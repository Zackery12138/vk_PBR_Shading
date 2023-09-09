#include "load_model_obj.hpp"

#include <unordered_set>

#include <cassert>
#include <cstring>

#include <rapidobj/rapidobj.hpp>

#include "../labutils/error.hpp"
#include "input_model.hpp"
namespace lut = labutils;

InputModel load_wavefront_obj( char const* aPath )
{
	assert( aPath );
	
	// Ask rapidobj to load the requested file
	auto result = rapidobj::ParseFile( aPath );
	if( result.error )
		throw lut::Error( "Unable to load OBJ file '%s': %s", aPath, result.error.code.message().c_str() );

	// OBJ files can define faces that are not triangles. However, Vulkan will
	// only render triangles (or lines and points), so we must triangulate any
	// faces that are not already triangles. Fortunately, rapidobj can do this
	// for us.
	rapidobj::Triangulate( result );

	// Find the path to the OBJ file
	char const* pathBeg = aPath;
	char const* pathEnd = std::strrchr( pathBeg, '/' );
	
	std::string const prefix = pathEnd
		? std::string( pathBeg, pathEnd+1 )
		: ""
	;

	// Convert the OBJ data into a InputModel structure.
	// First, extract material data.
	InputModel ret;

	ret.modelSourcePath = aPath;

	for( auto const& mat : result.materials )
	{
		InputMaterialInfo mi;

		mi.materialName  = mat.name;

		mi.baseColor   = glm::vec3( mat.diffuse[0], mat.diffuse[1], mat.diffuse[2] );

		mi.baseRoughness  = mat.roughness;
		mi.baseMetalness  = mat.metallic;

		if( !mat.diffuse_texname.empty() )
			mi.baseColorTexturePath  = prefix + mat.diffuse_texname;

		if( !mat.roughness_texname.empty() )
			mi.roughnessTexturePath  = prefix + mat.roughness_texname;
		if( !mat.metallic_texname.empty() )
			mi.metalnessTexturePath  = prefix + mat.metallic_texname;

		if( !mat.alpha_texname.empty() )
			mi.alphaMaskTexturePath  = prefix + mat.alpha_texname;

		if( !mat.normal_texname.empty() )
			mi.normalMapTexturePath  = prefix + mat.normal_texname;

#	if 0
		mi.diffuseColor  = glm::vec3( mat.diffuse[0], mat.diffuse[1], mat.diffuse[2] );

		if( !mat.diffuse_texname.empty() )
			mi.diffuseTexturePath  = prefix + mat.diffuse_texname;
#	endif

		ret.materials.emplace_back( std::move(mi) );
	}

	// Next, extract the actual mesh data. There are some complications:
	// - OBJ use separate indices to positions, normals and texture coords. To
	//   deal with this, the mesh is turned into an unindexed triangle soup.
	// - OBJ uses three methods of grouping faces:
	//   - 'o' = object
	//   - 'g' = group
	//   - 'usemtl' = switch materials
	//  The first two create logical objects/groups. The latter switches
	//  materials. We want to primarily group faces by material (and possibly
	//  secondarily by other logical groupings). 
	//
	// Unfortunately, RapidOBJ exposes a per-face material index.

	std::unordered_set<std::size_t> activeMaterials;
	for( auto const& shape : result.shapes )
	{
		auto const& shapeName = shape.name;

		// Scan shape for materials
		activeMaterials.clear();

		for( std::size_t i = 0; i < shape.mesh.indices.size(); ++i )
		{
			auto const faceId = i/3; // Always triangles; see Triangulate() above

			assert( faceId < shape.mesh.material_ids.size() );
			auto const matId = shape.mesh.material_ids[faceId];

			assert( matId < int(ret.materials.size()) );
			activeMaterials.emplace( matId );
		}

		// Process vertices for active material
		// This does multiple passes over the vertex data, which is less than
		// optimal...
		//
		// Note: we still keep different "shapes" separate. For static meshes,
		// one could merge all vertices with the same material for a bit more
		// efficient rendering.
		for( auto const matId : activeMaterials )
		{
			// Keep track of mesh names; this can be useful for debugging.
			std::string meshName;
			if( 1 == activeMaterials.size() )
				meshName = shapeName;
			else
				meshName = shapeName + "::" + ret.materials[matId].materialName;

			// Extract this material's vertices.
			auto const firstVertex = ret.positions.size();
			
			for( std::size_t i = 0; i < shape.mesh.indices.size(); ++i )
			{
				auto const faceId = i/3; // Always triangles; see Triangulate() above
				auto const faceMat = std::size_t(shape.mesh.material_ids[faceId]);

				if( faceMat != matId )
					continue;

				auto const& idx = shape.mesh.indices[i];

				ret.positions.emplace_back( glm::vec3{
					result.attributes.positions[idx.position_index*3+0],
					result.attributes.positions[idx.position_index*3+1],
					result.attributes.positions[idx.position_index*3+2]
				} );

				ret.texcoords.emplace_back( glm::vec2{
					result.attributes.texcoords[idx.texcoord_index*2+0],
					result.attributes.texcoords[idx.texcoord_index*2+1]
				} );

				ret.normals.emplace_back( glm::vec3{
					result.attributes.normals[idx.normal_index*3+0],
					result.attributes.normals[idx.normal_index*3+1],
					result.attributes.normals[idx.normal_index*3+2]
				} );
			}

			auto const vertexCount = ret.positions.size() - firstVertex;

			ret.meshes.emplace_back( InputMeshInfo{
				std::move(meshName),
				matId,
				firstVertex,
				vertexCount
			} );
		}
	}

	return ret;
}

