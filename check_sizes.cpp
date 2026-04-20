
#include <iostream>
#include <stddef.h>

typedef int BlendMode;
typedef int OnOffState;

struct RenderState
{
	BlendMode blendMode;
	OnOffState alphaTest;
	int alphaTestRef;
	OnOffState culling;
};

struct SamplerState
{
	int minFilter;
	int magFilter;
	int mipFilter;
};

struct Sampler
{
    SamplerState samplerState;
    void* pTexture;
};

struct AOEMaterial
{
    virtual ~AOEMaterial() {}
    RenderState renderState;
    Sampler DiffuseMap;
};

int main() {
    std::cout << "sizeof(RenderState): " << sizeof(RenderState) << std::endl;
    std::cout << "sizeof(Sampler): " << sizeof(Sampler) << std::endl;
    std::cout << "offsetof(AOEMaterial, renderState): " << offsetof(AOEMaterial, renderState) << std::endl;
    std::cout << "offsetof(AOEMaterial, DiffuseMap): " << offsetof(AOEMaterial, DiffuseMap) << std::endl;
    std::cout << "gap: " << (offsetof(AOEMaterial, DiffuseMap) - (offsetof(AOEMaterial, renderState) + sizeof(RenderState))) << std::endl;
    return 0;
}
