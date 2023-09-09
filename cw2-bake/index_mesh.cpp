#include "index_mesh.hpp"

#include <numeric>
#include <unordered_map>

#include <cstddef>

#include <glm/glm.hpp>
#include <tgen.h>

namespace
{
	// Tweakables
	constexpr float kAABBMarginFactor = 10.f;
	constexpr std::size_t kSparseGridMaxSize = 1024*1024;

	// Discretize mesh positions
	struct DiscretizedPosition_
	{
		std::int32_t x, y, z;
	};

	struct Discretizer_
	{
		Discretizer_( std::uint32_t aFactor, glm::vec3, float );
		inline DiscretizedPosition_ discretize( glm::vec3 const& ) const;

		glm::vec3 min;
		float scale;
	};

	// hash discretized mesh positions
	using VicinityKey_ = std::size_t;
	inline VicinityKey_ hash_discretized_position_( DiscretizedPosition_ const& aPos );

	// generate vicinity map 
	using VicinityMap_ = std::unordered_multimap<VicinityKey_,std::size_t>;
	void build_vicinity_map_( 
		VicinityMap_&, 
		Discretizer_ const&,
		std::vector<glm::vec3> const&
	);

	// is a vertex mergable?
	bool mergable_( 
		TriangleSoup const&, 
		std::size_t aVertexAIndex, std::size_t aVertexBIndex,
		glm::vec3 const& aVertexAPos, glm::vec3 const& aVertexBPos,
		float
	);

	// collapse vertices
	using VertexMapping_ = std::vector<std::size_t>;
	using IndexBuffer_ = std::vector<std::uint32_t>;

	std::size_t collapse_vertices_( 
		IndexBuffer_&, 
		VertexMapping_&, 
		VicinityMap_ const&, 
		Discretizer_ const&, 
		TriangleSoup const&, 
		float
	);

}

//--    IndexedMesh                     ///{{{2///////////////////////////////
IndexedMesh::IndexedMesh()
	: aabbMin( std::numeric_limits<float>::max() )
	, aabbMax( std::numeric_limits<float>::min() )
{}

//--    make_indexed_mesh()             ///{{{2///////////////////////////////
IndexedMesh make_indexed_mesh( TriangleSoup const& aSoup, float aErrorTolerance )
{
	// compute bounding volume
	glm::vec3 bmin( std::numeric_limits<float>::max() );
	glm::vec3 bmax( std::numeric_limits<float>::min() );

	for( std::size_t vert = 0; vert < aSoup.vert.size(); ++vert )
	{
		bmin = min( bmin, aSoup.vert[vert] );
		bmax = max( bmax, aSoup.vert[vert] );
	}

	auto const fmin = bmin - glm::vec3( kAABBMarginFactor * aErrorTolerance );
	auto const fmax = bmax + glm::vec3( kAABBMarginFactor * aErrorTolerance );

	// Compute grid size
	auto const side = fmax - fmin;
	float const maxSide = std::max( side.x, std::max( side.y, side.z ) );

	float const numCells = maxSide / (2.f*aErrorTolerance);
	std::size_t subdiv = std::min( kSparseGridMaxSize, std::size_t(numCells+.5f) );

	// parameters for discretization
	Discretizer_ dis( std::uint32_t(subdiv), fmin, maxSide );

	// build the vincinity map
	VicinityMap_ vincinityMap;
	build_vicinity_map_( vincinityMap, dis, aSoup.vert );

	// collapse vertices
	IndexBuffer_ indices;
	VertexMapping_ vertexMapping;

	size_t verts = collapse_vertices_( indices, vertexMapping, vincinityMap, dis, aSoup, aErrorTolerance );

	assert( indices.size() == aSoup.vert.size() );
	assert( verts == vertexMapping.size() );

	// shuffle vertex data
	IndexedMesh ret;
		
	ret.vert.resize( verts );
	ret.text.resize( verts );

	if( !aSoup.norm.empty() )
		ret.norm.resize( verts );

	for( size_t i = 0; i < verts; ++i )
	{
		size_t const from = vertexMapping[i];
		assert( from < aSoup.vert.size() );

		ret.vert[i] = aSoup.vert[from];
		ret.text[i] = aSoup.text[from];

		if( !aSoup.norm.empty() )
			ret.norm[i] = aSoup.norm[from];
	}

	ret.indices = std::move(indices);
	
	std::vector<tgen::RealT> positions, uvs, normals;
	for (const auto& v : ret.vert) {
		positions.push_back(v.x); 
		positions.push_back(v.y); 
		positions.push_back(v.z); 
	}
	for (const auto& uv : ret.text) {
		uvs.push_back(uv.x); 
		uvs.push_back(uv.y); 
	}
	for (const auto& n : ret.norm) {
		normals.push_back(n.x); 
		normals.push_back(n.y); 
		normals.push_back(n.z); 
	}
	std::vector<tgen::RealT> tangents4D;


	{
		std::vector<tgen::VIndexT> tgenIndices(ret.indices.begin(), ret.indices.end());
		std::vector<tgen::RealT> cornerTangents3D, cornerBitangents3D;
		std::vector<tgen::RealT> vertexTangents3D, vertexBitangents3D;

		tgen::computeCornerTSpace(tgenIndices, tgenIndices, positions, uvs, cornerTangents3D, cornerBitangents3D);
		tgen::computeVertexTSpace(tgenIndices, cornerTangents3D, cornerBitangents3D, ret.text.size(), vertexTangents3D, vertexBitangents3D);
		tgen::orthogonalizeTSpace(normals, vertexTangents3D, vertexBitangents3D);
		tgen::computeTangent4D(normals, vertexTangents3D, vertexBitangents3D, tangents4D);
	}

	for (size_t i = 0; i < tangents4D.size(); i += 4) {
		ret.tangent.push_back(glm::vec4(tangents4D[i], tangents4D[i+1], tangents4D[i+2], tangents4D[i+3]));
	}
	
	ret.aabbMin = bmin;
	ret.aabbMax = bmax;

	return ret;
}

#if 0
//--    ensure_normals()                ///{{{2///////////////////////////////
void ensure_normals( IndexedMesh& aMesh )
{
	// Got normals? Done.
	if( !aMesh.norm.empty() )
		return;

	// Nope?
	aMesh.norm.resize( aMesh.vert.size(), glm::vec3(0.f) );

	for( size_t face = 0; face < aMesh.indices.size()/3; ++face )
	{
		size_t const idx = face*3;
		size_t const i = aMesh.indices[idx+0];
		size_t const j = aMesh.indices[idx+1];
		size_t const k = aMesh.indices[idx+2];

		{
			glm::vec3 const a = aMesh.vert[j] - aMesh.vert[i];
			glm::vec3 const b = aMesh.vert[k] - aMesh.vert[i];
			glm::vec3 const c = cross( a, b );
			aMesh.norm[i] += normalize(c);
		}
		{
			glm::vec3 const a = aMesh.vert[k] - aMesh.vert[j];
			glm::vec3 const b = aMesh.vert[i] - aMesh.vert[j];
			glm::vec3 const c = cross( a, b );
			aMesh.norm[j] += normalize(c);
		}
		{
			glm::vec3 const a = aMesh.vert[i] - aMesh.vert[k];
			glm::vec3 const b = aMesh.vert[j] - aMesh.vert[k];
			glm::vec3 const c = cross( a, b );
			aMesh.norm[k] += normalize(c);
		}
	}

	for( auto& n : aMesh.norm )
		n = normalize(n);
}
#endif


//--    $ local functions               ///{{{2///////////////////////////////
namespace
{
	Discretizer_::Discretizer_( std::uint32_t aFactor, glm::vec3 aMin, float aSide )
	{
		min = aMin;
		scale = aFactor / aSide;
	}

	inline
	DiscretizedPosition_ Discretizer_::discretize( glm::vec3 const& aPos ) const
	{
		DiscretizedPosition_ ret;
		ret.x = std::uint32_t((aPos[0]-min[0])*scale);
		ret.y = std::uint32_t((aPos[1]-min[1])*scale);
		ret.z = std::uint32_t((aPos[2]-min[2])*scale);
		return ret;
	}
}

namespace
{
	std::hash<VicinityKey_> gHash_;

	inline VicinityKey_ hash_discretized_position_( DiscretizedPosition_ const& aDP )
	{
		// Based on boost::hash_combine.
		std::size_t hash = gHash_(aDP.x);
		hash ^= gHash_(aDP.y) + 0x9e3779b9 + (hash<<6) + (hash>>2);
		hash ^= gHash_(aDP.z) + 0x9e3779b9 + (hash<<6) + (hash>>2);
		return hash;
	}
}

namespace
{
	void build_vicinity_map_( VicinityMap_& aMap, Discretizer_ const& aD, std::vector<glm::vec3> const& aPositions )
	{
		for( std::size_t index = 0; index < aPositions.size(); ++index )
		{
			DiscretizedPosition_ dp = aD.discretize( aPositions[index] );
			VicinityKey_ vk = hash_discretized_position_( dp );

			aMap.insert( std::make_pair(vk, index) );
		}
	}
}

namespace
{
	bool mergable_( TriangleSoup const& aSoup, size_t aI, size_t aJ, glm::vec3 const& aIPos, glm::vec3 const& aJPos, float aErrorTolerance )
	{
		// Compare all elements component-wise. 
		// start with positions, since we've already got those
		for( std::size_t i = 0; i < 3; ++i )
		{
			if( std::abs(aIPos[i]-aJPos[i]) > aErrorTolerance )
				return false;
		}

		// Compare normals
		if( !aSoup.norm.empty() )
		{
			auto const nI = aSoup.norm[aI];
			auto const nJ = aSoup.norm[aJ];
			for( size_t i = 0; i < 3; ++i )
			{
				if( std::abs(nI[i]-nJ[i]) > aErrorTolerance )
					return false;
			}
		}

		// Compare tex coord
		auto const tI = aSoup.text[aI];
		auto const tJ = aSoup.text[aJ];
		for( std::size_t i = 0; i < 2; ++i )
		{
			if( std::abs(tI[i]-tJ[i]) > aErrorTolerance )
				return false;
		}
	
		return true;
	}
}

namespace
{
	// neighbours
	const size_t kNeighbourCount_ = 27;

	DiscretizedPosition_ neighbour_( DiscretizedPosition_ const& aDP, std::size_t aJ )
	{
		static constexpr std::int32_t offset[kNeighbourCount_][3] = {
			{ 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
			{ 0, 1, 0 }, { 0, 1, 1 }, { 0, 1, -1 },
			{ 0, -1, 0 }, { 0, -1, 1 }, { 0, -1, -1 },

			{ 1, 0, 0 }, { 1, 0, 1 }, { 1, 0, -1 },
			{ 1, 1, 0 }, { 1, 1, 1 }, { 1, 1, -1 },
			{ 1, -1, 0 }, { 1, -1, 1 }, { 1, -1, -1 },

			{ -1, 0, 0 }, { -1, 0, 1 }, { -1, 0, -1 },
			{ -1, 1, 0 }, { -1, 1, 1 }, { -1, 1, -1 },
			{ -1, -1, 0 }, { -1, -1, 1 }, { -1, -1, -1 },
		};

		assert( aJ < kNeighbourCount_ );
		
		DiscretizedPosition_ ret = aDP;
		ret.x += offset[aJ][0];
		ret.y += offset[aJ][1];
		ret.z += offset[aJ][2];
		return ret;
	}

	// Merge vertices
	size_t collapse_vertices_( IndexBuffer_& aIndices, VertexMapping_& aVertices, VicinityMap_ const& aVM, Discretizer_ const& aD, TriangleSoup const& aSoup, float aMaxError )
	{
		aVertices.clear();
		aVertices.reserve( aSoup.vert.size() );

		aIndices.clear();
		aIndices.reserve( aSoup.vert.size() );

		// initialize collapse map
		VertexMapping_ collapseMap( aSoup.vert.size() );
		std::fill( collapseMap.begin(), collapseMap.end(), ~std::size_t(0) );

		// process vertices
		std::size_t nextVertex = 0;
		for( std::size_t i = 0; i < aSoup.vert.size(); ++i )
		{
			// check if this vertex already was merged somewhere
			if( ~size_t(0) != collapseMap[i] )
			{
				assert( collapseMap[i] < aVertices.size() );
				aIndices.push_back( std::uint32_t(collapseMap[i]) );
				continue;
			}

			// get position and look for possible neighbours
			auto const self = aSoup.vert[i];
			DiscretizedPosition_ const dp = aD.discretize( self );

			bool merged = false;
			std::size_t target = ~std::size_t(0);

			for( std::size_t j = 0; j < kNeighbourCount_; ++j )
			{
				DiscretizedPosition_ const dq = neighbour_( dp, j );
				VicinityKey_ const vk = hash_discretized_position_( dq );

				// get vertices in this bucket
				for( auto [it, jt] = aVM.equal_range( vk ); it != jt; ++it )
				{
					std::size_t const idx =  it->second;

					if( idx == i ) continue; // don't try to merge with self
					if( ~std::size_t(0) != collapseMap[idx] ) continue; // don't remerge

					auto const other = aSoup.vert[idx];
					if( mergable_( aSoup, i, idx, self, other, aMaxError ) )
					{
						std::size_t toWhere;
						
						if( merged )
						{
							toWhere = target;
						}
						else
						{
							toWhere = nextVertex++;
							aVertices.push_back( i );

							collapseMap[i] = toWhere;
							aIndices.push_back( std::uint32_t(toWhere) );
						}

						collapseMap[idx] = toWhere;
						
						target = toWhere;
						merged = true;
					}
				}
			}

			if( !merged )
			{
				std::size_t toWhere = nextVertex++;

				collapseMap[i] = toWhere;
				aVertices.push_back( i );
				aIndices.push_back( std::uint32_t(toWhere) );
			}
		}

		return nextVertex;
	}
}

//--///}}}1/////////////// vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
