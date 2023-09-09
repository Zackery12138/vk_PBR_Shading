#ifndef INPUT_MODEL_HPP_69C371FB_85B1_4E88_B333_F31BCDF073B9
#define INPUT_MODEL_HPP_69C371FB_85B1_4E88_B333_F31BCDF073B9

#include <string>
#include <vector>

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct InputMaterialInfo
{
	std::string materialName;  // This is purely informational and for debugging

	glm::vec3 baseColor;

	float baseRoughness;
	float baseMetalness;

	std::string baseColorTexturePath;
	std::string roughnessTexturePath;
	std::string metalnessTexturePath;
	std::string alphaMaskTexturePath;   // see note below
	std::string normalMapTexturePath;

	/* Note: you may assume that if alphaMaskTexturePath is set, it is equal to
	 * baseColorTexturePath. In this case, the corresponding texture is an RGBA
	 * texture (e.g. stored as a PNG), and the alpha channel encodes the alpha
	 * mask.
	 */
};

struct InputMeshInfo
{
	std::string meshName;  // This is purely informational and for debugging
	
	std::size_t materialIndex;

	std::size_t vertexStartIndex;
	std::size_t vertexCount;
};

struct InputModel
{
	std::string modelSourcePath;

	std::vector<InputMaterialInfo> materials;
	std::vector<InputMeshInfo> meshes;

	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> texcoords;
};

#endif // INPUT_MODEL_HPP_69C371FB_85B1_4E88_B333_F31BCDF073B9

