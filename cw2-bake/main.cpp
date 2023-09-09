#include <iterator>
#include <vector>
#include <typeinfo>
#include <exception>
#include <filesystem>
#include <system_error>
#include <unordered_map>

#include <cstdio>
#include <cstring>

#include <tgen.h>
#include <glm/glm.hpp>

#include "index_mesh.hpp"
#include "input_model.hpp"
#include "load_model_obj.hpp"

#include "../labutils/error.hpp"
namespace lut = labutils;

namespace
{
	// constants
	/* File "magic". The first 16 bytes of our custom file are equal to this
	 * magic value. This allows us to check whether a certain file is
	 * (probably) of the right type. Having a file magic is relatively common
	 * practice -- you can find a list of such magic sequences e.g. here:
     * https://en.wikipedia.org/wiki/List_of_file_signatures
	 *
	 * When picking a signature there are a few considerations. For example,
	 * including non-printable characters (e.g. the \0) early keeps the file
	 * from being misidentified as text.
	 */
	 constexpr char kFileMagic[16] = "\0\0COMP5822Mmesh";

	/* Note: change the file variant if you change the file format! 
	 *
	 * Suggestion: use 'uid-tag'. For example, I would use "scsmbil-tan" to
	 * indicate that this is a custom format by myself (=scsmbil) with
	 * additional tangent space information.
	 */
	constexpr char kFileVariant[16] = "scsmbil-tan";

	// types
	struct TextureInfo_
	{
		std::uint32_t uniqueId;
		std::uint8_t channels;
		std::string newPath;
	};

	// local functions:
	void process_model_(
		char const* aOutput,
		char const* aInputOBJ,
		glm::mat4x4 const& aStaticTransform = glm::mat4x4( 1.f ) //TODO
	);


	void write_model_data_(
		FILE*,
		InputModel const&,
		std::vector<IndexedMesh> const&,
		std::unordered_map<std::string,TextureInfo_> const&
	);


	std::vector<IndexedMesh> index_meshes_(
		InputModel const&,
		float aErrorTolerance = 1e-5f
	);

	std::unordered_map<std::string,TextureInfo_> find_unique_textures_(
		InputModel const&
	);

	std::unordered_map<std::string,TextureInfo_> new_paths_(
		std::unordered_map<std::string,TextureInfo_>,
		std::filesystem::path const& aTexDir
	);
}


int main() try
{
	process_model_(
		"assets/cw2/sponza-pbr.comp5822mesh",
		"assets-src/cw2/sponza-pbr.obj"
	);

	return 0;
}
catch( std::exception const& eErr )
{
	std::fprintf( stderr, "Top-level exception [%s]:\n%s\nBye.\n", typeid(eErr).name(), eErr.what() );
	return 1;
}

namespace
{
	void process_model_( char const* aOutput, char const* aInputOBJ, glm::mat4x4 const& aStaticTransform )
	{
		static constexpr std::size_t vertexSize = sizeof(float)*(3+3+2);

		// Figure out output paths
		std::filesystem::path const outname( aOutput );
		std::filesystem::path const rootdir = outname.parent_path();
		std::filesystem::path const basename = outname.stem();
		std::filesystem::path const texdir = basename.string() + "-tex";

		// Load input model
		auto const model = load_wavefront_obj( aInputOBJ );

		std::size_t inputVerts = 0;
		for( auto const& imesh : model.meshes )
			inputVerts += imesh.vertexCount;

		std::printf( "%s: %zu meshes, %zu materials\n", aInputOBJ, model.meshes.size(), model.materials.size() );
		std::printf( " - triangle soup vertices: %zu => %zu kB\n", inputVerts, inputVerts*vertexSize/1024 );

		// Index meshes
		auto const indexed = index_meshes_( model );

		std::size_t outputVerts = 0, outputIndices = 0, outputTangents = 0;
		for( auto const& mesh : indexed )
		{
			outputVerts += mesh.vert.size();
			outputIndices += mesh.indices.size();
			outputTangents += mesh.tangent.size();
		}

		std::printf( " - indexed vertices: %zu with %zu indices => %zu kB\n", outputVerts, outputIndices, (outputVerts*vertexSize + outputIndices*sizeof(std::uint32_t))/1024 );
		std::printf(" - tangents: %zu\n", outputTangents);

		// Find list of unique textures
		auto const textures = new_paths_( find_unique_textures_( model ), texdir );

		std::printf( " - unique textures: %zu\n", textures.size() );

		// Ensure output directory exists
		std::filesystem::create_directories( rootdir );

		// Output mesh data
		auto mainpath = rootdir / basename;
		mainpath.replace_extension( "comp5822mesh" );

		FILE* fof = std::fopen( mainpath.string().c_str(), "wb" );
		if( !fof )
			throw lut::Error( "Unable to open '%s' for writing", mainpath.string().c_str() );

		try
		{
			write_model_data_( fof, model, indexed, textures );
		}
		catch( ... )
		{
			std::fclose( fof );
			throw;
		}

		std::fclose( fof );

		// Copy textures
		std::filesystem::create_directories( rootdir / texdir );

		std::size_t errors = 0;
		for( auto const& entry : textures )
		{
			auto const dest = rootdir / entry.second.newPath;

			std::error_code ec;
			bool ret = std::filesystem::copy_file( 
				entry.first,
				dest,
				std::filesystem::copy_options::none,
				ec
			);

			if( !ret )
			{
				++errors;
				std::fprintf( stderr, "copy_file(): '%s' failed: %s (%s)\n", dest.string().c_str(), ec.message().c_str(), ec.category().name() );
			}
		}

		auto const total = textures.size();
		std::printf( "Copied %zu textures out of %zu.\n", total-errors, total );
		if( errors )
		{
			std::fprintf( stderr, "Some copies reported an error. Currently, the code will never overwrite existing files. The errors likely just indicate that the file was copied previously. Remove old files manually, if necessary.\n" );
		}
	}
}

namespace
{
	void checked_write_( FILE* aOut, std::size_t aBytes, void const* aData )
	{
		auto const ret = std::fwrite( aData, 1, aBytes, aOut );

		if( ret != aBytes )
			throw lut::Error( "fwrite() failed: %zu instead of %zu", ret, aBytes );
	}

	void write_string_( FILE* aOut, char const* aString )
	{
		// Write a string
		// Format:
		//  - uint32_t : N = length of string in bytes, including terminating '\0'
		//  - N x char : string
		std::uint32_t const length = std::uint32_t(std::strlen(aString)+1);
		checked_write_( aOut, sizeof(std::uint32_t), &length );

		checked_write_( aOut, length, aString );
	}

	void write_model_data_( FILE* aOut, InputModel const& aModel, std::vector<IndexedMesh> const& aIndexedMeshes, std::unordered_map<std::string,TextureInfo_> const& aTextures )
	{
		// Write header
		// Format:
		//   - char[16] : file magic
		//   - char[16] : file variant ID
		checked_write_( aOut, sizeof(char)*16, kFileMagic );
		checked_write_( aOut, sizeof(char)*16, kFileVariant );
		
		// Write list of unique textures
		// Format:
		//  - unit32_t : U = number of unique textures
		//  - repeat U times:
		//    - string : path to texture 
		//    - uint8_t : number of channels in texture
		std::vector<TextureInfo_ const*> orderedUnqiue( aTextures.size() );
		for( auto const& tex : aTextures )
		{
			assert( !orderedUnqiue[tex.second.uniqueId] );
			orderedUnqiue[tex.second.uniqueId] = &tex.second;
		}

		std::uint32_t const textureCount = std::uint32_t(orderedUnqiue.size());
		checked_write_( aOut, sizeof(textureCount), &textureCount );

		for( auto const& tex : orderedUnqiue )
		{
			assert( tex );
			write_string_( aOut, tex->newPath.c_str() );

			std::uint8_t channels = tex->channels;
			checked_write_( aOut, sizeof(channels), &channels );
		}

		// Write material information
		// Format:
		//  - uint32_t : M = number of materials
		//  - repeat M times:
		//    - uin32_t : base color texture index
		//    - uin32_t : roughness texture index
		//    - uin32_t : metalness texture index
		//    - uin32_t : alphaMask texture index (or 0xffffffff if none)
		//    - uin32_t : normalMap texture index (or 0xffffffff if none)
		//    - TODO: base color, metalness and roughness
		std::uint32_t const materialCount = std::uint32_t(aModel.materials.size());
		checked_write_( aOut, sizeof(materialCount), &materialCount );

		for( auto const& mat : aModel.materials )
		{
			auto const write_tex_ = [&] (std::string const& aTexturePath ) {
				if( aTexturePath.empty() )
				{
					static constexpr std::uint32_t sentinel = ~std::uint32_t(0);
					checked_write_( aOut, sizeof(std::uint32_t), &sentinel );
					return;
				}

				auto const it = aTextures.find( aTexturePath );
				assert( aTextures.end() != it );

				checked_write_( aOut, sizeof(std::uint32_t), &it->second.uniqueId );
			};

			write_tex_( mat.baseColorTexturePath );
			write_tex_( mat.roughnessTexturePath );
			write_tex_( mat.metalnessTexturePath );
			write_tex_( mat.alphaMaskTexturePath );
			write_tex_( mat.normalMapTexturePath );
		}

		// Write mesh data
		// Format:
		//  - uint32_t : M = number of meshes
		//  - repeat M times:
		//    - uint32_t : material index
		//    - uint32_t : V = number of vertices
		//    - uint32_t : I = number of indices
		//    - repeat V times: vec3 position
		//    - repeat V times: vec3 normal
		//    - repeat V times: vec2 texture coordinate
		//    - repeat I times: uint32_t index
		std::uint32_t const meshCount = std::uint32_t(aModel.meshes.size());
		checked_write_( aOut, sizeof(meshCount), &meshCount );

		assert( aModel.meshes.size() == aIndexedMeshes.size() );
		for( std::size_t i = 0; i < aModel.meshes.size(); ++i )
		{
			auto const& mmesh = aModel.meshes[i];

			std::uint32_t materialIndex = std::uint32_t(mmesh.materialIndex);
			checked_write_( aOut, sizeof(materialIndex), &materialIndex );

			auto const& imesh = aIndexedMeshes[i];

			std::uint32_t vertexCount = std::uint32_t(imesh.vert.size());
			checked_write_( aOut, sizeof(vertexCount), &vertexCount );
			std::uint32_t indexCount = std::uint32_t(imesh.indices.size());
			checked_write_( aOut, sizeof(indexCount), &indexCount );

			checked_write_( aOut, sizeof(glm::vec3)*vertexCount, imesh.vert.data() );
			checked_write_( aOut, sizeof(glm::vec3)*vertexCount, imesh.norm.data() );
			checked_write_( aOut, sizeof(glm::vec2)*vertexCount, imesh.text.data() );
			checked_write_( aOut, sizeof(glm::vec4)*vertexCount, imesh.tangent.data());

			checked_write_( aOut, sizeof(std::uint32_t)*indexCount, imesh.indices.data() );
		}
	}
}

namespace
{
	std::vector<IndexedMesh> index_meshes_( InputModel const& aModel, float aErrorTolerance )
	{
		std::vector<IndexedMesh> indexed;

		for( auto const& imesh : aModel.meshes )
		{
			auto const endIndex = imesh.vertexStartIndex + imesh.vertexCount;

			TriangleSoup soup;

			soup.vert.reserve( imesh.vertexCount );
			for( std::size_t i = imesh.vertexStartIndex; i < endIndex; ++i )
				soup.vert.emplace_back( aModel.positions[i] );

			soup.text.reserve( imesh.vertexCount );
			for( std::size_t i = imesh.vertexStartIndex; i < endIndex; ++i )
				soup.text.emplace_back( aModel.texcoords[i] );

			soup.norm.reserve( imesh.vertexCount );
			for( std::size_t i = imesh.vertexStartIndex; i < endIndex; ++i )
				soup.norm.emplace_back( aModel.normals[i] );
			

			indexed.emplace_back( make_indexed_mesh( soup, aErrorTolerance ) );
		}

		return indexed;
	}
}

namespace
{
	std::unordered_map<std::string,TextureInfo_> find_unique_textures_( InputModel const& aModel )
	{
		std::unordered_map<std::string,TextureInfo_> unique;

		std::uint32_t texid = 0;
		auto const add_unique_ = [&] (std::string const& aPath, std::uint8_t aChannels)
		{
			if( aPath.empty() )
				return;

			TextureInfo_ info{};
			info.uniqueId = texid;
			info.channels = aChannels;

			auto const [it, isNew] = unique.emplace( std::make_pair(aPath,info) );

			if( isNew )
				++texid;
		};

		for( auto const& mat : aModel.materials )
		{
			add_unique_( mat.baseColorTexturePath, 4 );
			add_unique_( mat.roughnessTexturePath, 1 ); 
			add_unique_( mat.metalnessTexturePath, 1 ); 
			add_unique_( mat.alphaMaskTexturePath, 4 );  // assume == baseColor
			add_unique_( mat.normalMapTexturePath, 4 );  // eh...
		}

		return unique;
	}

	std::unordered_map<std::string,TextureInfo_> new_paths_( std::unordered_map<std::string,TextureInfo_> aTextures, std::filesystem::path const& aTexDir )
	{
		for( auto& entry : aTextures )
		{
			std::filesystem::path const originalPath( entry.first );
			auto const filename = originalPath.filename();
			auto const newpath = aTexDir / filename;
		
			auto& info = entry.second;
			info.newPath = newpath.string();
		}

		// Note: aTextures is still local to the function, so there is no need
		// to explicitly std::move() it. However, since it is passed in as an
		// argument, NRVO is unlikely to occur.
		return aTextures; 
	}
}


