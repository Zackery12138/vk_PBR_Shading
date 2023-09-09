The provided "sponza-pbr.obj" model has been generated from the glTF Sponza
model available at 

  https://github.com/KhronosGroup/glTF-Sample-Models

See https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0/Sponza
for licensing notes.


While converting it to OBJ with PBR extensions, the following changes have
taken place:

- The combined metalness-roughness texture has been split into separate 
  metalness (map_Pm) and roughness (map_Pr) textures.
- The normal map uses the 'norm' material field, part of the PBR extension.
  (The standard 'map_bump' expects a bump map, which -strictly speaking- isn't
  a normal map.)
- Object group and material names were generated programmatically and are 
  unlikely to match those of any existing Sponza model.

Additionally, for COMP5822M Coursework 2, the metalness texture for the lion
head reliefs has been edited to change the material to a metal rather than a
dielectric. The texture affected is m-13196865903111448057.jpg.
