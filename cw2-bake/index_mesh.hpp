#ifndef INDEX_MESH_HPP_8617BC10_313B_4397_9E27_33AA16A4C308
#define INDEX_MESH_HPP_8617BC10_313B_4397_9E27_33AA16A4C308

//--//////////////////////////////////////////////////////////////////////////
//--    include                                 ///{{{1///////////////////////

#include <vector>

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

//--    types                                   ///{{{1///////////////////////
struct TriangleSoup
{
	std::vector<glm::vec3> vert;
	std::vector<glm::vec3> norm;
	std::vector<glm::vec2> text;
};
struct IndexedMesh
{
	std::vector<glm::vec3> vert;
	std::vector<glm::vec3> norm;
	std::vector<glm::vec2> text;

	std::vector<glm::vec4> tangent; // Task 1.4 

	std::vector<std::uint32_t> indices;

	glm::vec3 aabbMin, aabbMax;

	IndexedMesh();
};

//--    functions                               ///{{{1///////////////////////

IndexedMesh make_indexed_mesh(
	TriangleSoup const&,
	float aErrorTol = 1e-6f
);

void ensure_normals( IndexedMesh& );

#endif // INDEX_MESH_HPP_8617BC10_313B_4397_9E27_33AA16A4C308
