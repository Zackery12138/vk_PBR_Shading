#version 450
layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;
layout( location = 3 ) in vec4 iTangent;

layout(std140,set = 0, binding = 0) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	vec3 cameraPos;
	vec3 lightPos;
	vec3 lightColor;
}uScene;

layout( location = 0 ) out vec2 v2fTexCoords;
layout( location = 1 ) out vec3 v2fNormal;
layout( location = 2 ) out vec3 v2fPosition;
layout( location = 3 ) out vec4 v2fTangent;

void main()
{
	v2fTangent = iTangent;
	v2fPosition = iPosition;
	v2fTexCoords = iTexCoord;
	v2fNormal = iNormal;
	gl_Position = uScene.projCam * vec4(iPosition, 1.0f);
}