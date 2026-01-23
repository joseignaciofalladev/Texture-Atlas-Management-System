// ============================================================
// Texture Atlas Management System
// Objetivo:
// - Reducir cambios de estado de GPU
// - Minimizar draw calls
// - Mejorar coherencia de caché
// - Controlar mipmaps, padding y ciclos de vida
// Filosofía:
// - Un atlas = una unidad de render
// - Agrupar por uso, distancia y material
// - Nunca mezclar ciclos de vida incompatibles

#include "TXTextureAtlasSystem.h"

// ------------------------------------------------------------
// Configuración base
// ------------------------------------------------------------

static const uint32_t ATLAS_PADDING_PIXELS = 4;
static const uint32_t ATLAS_MAX_SIZE       = 2048;

// Utilidades internas
static uint32_t Align(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// Constructor
TXTextureAtlasSystem::TXTextureAtlasSystem()
{
    m_Atlases.reserve(32);
}

// ------------------------------------------------------------
// Creación de un nuevo atlas
// ------------------------------------------------------------

TXTextureAtlas* TXTextureAtlasSystem::CreateAtlas(
    uint32_t width,
    uint32_t height,
    TXTextureFormat format,
    TXAtlasUsage usage)
{
    width  = Align(width,  128);
    height = Align(height, 128);

    if (width > ATLAS_MAX_SIZE || height > ATLAS_MAX_SIZE)
        return nullptr;

    TXTextureAtlas* atlas = new TXTextureAtlas();
    atlas->Width  = width;
    atlas->Height = height;
    atlas->Format = format;
    atlas->Usage  = usage;

    atlas->FreeRectangles.clear();
    atlas->UsedEntries.clear();

    // Inicialmente todo el atlas está libre
    TXAtlasRect root;
    root.X = 0;
    root.Y = 0;
    root.W = width;
    root.H = height;

    atlas->FreeRectangles.push_back(root);

    m_Atlases.push_back(atlas);
    return atlas;
}

// ------------------------------------------------------------
// Inserción de una textura en el atlas
// ------------------------------------------------------------

TXAtlasEntry* TXTextureAtlasSystem::InsertTexture(
    TXTextureAtlas* atlas,
    uint32_t texWidth,
    uint32_t texHeight,
    const void* pixelData)
{
    texWidth  += ATLAS_PADDING_PIXELS * 2;
    texHeight += ATLAS_PADDING_PIXELS * 2;

    for (size_t i = 0; i < atlas->FreeRectangles.size(); ++i)
    {
        TXAtlasRect freeRect = atlas->FreeRectangles[i];

        if (texWidth <= freeRect.W && texHeight <= freeRect.H)
        {
            TXAtlasEntry entry;

            entry.X = freeRect.X + ATLAS_PADDING_PIXELS;
            entry.Y = freeRect.Y + ATLAS_PADDING_PIXELS;
            entry.W = texWidth  - ATLAS_PADDING_PIXELS * 2;
            entry.H = texHeight - ATLAS_PADDING_PIXELS * 2;

            // Copia de píxeles al atlas
            UploadTextureRegion(
                atlas,
                entry.X,
                entry.Y,
                entry.W,
                entry.H,
                pixelData);

            atlas->UsedEntries.push_back(entry);

            // División del rectángulo libre (bin packing simple)
            TXAtlasRect right;
            right.X = freeRect.X + texWidth;
            right.Y = freeRect.Y;
            right.W = freeRect.W - texWidth;
            right.H = texHeight;

            TXAtlasRect bottom;
            bottom.X = freeRect.X;
            bottom.Y = freeRect.Y + texHeight;
            bottom.W = freeRect.W;
            bottom.H = freeRect.H - texHeight;

            atlas->FreeRectangles.erase(
                atlas->FreeRectangles.begin() + i);

            if (right.W > 0 && right.H > 0)
                atlas->FreeRectangles.push_back(right);

            if (bottom.W > 0 && bottom.H > 0)
                atlas->FreeRectangles.push_back(bottom);

            return &atlas->UsedEntries.back();
        }
    }

    return nullptr;
}

// ------------------------------------------------------------
// Enlace del atlas para render
// ------------------------------------------------------------

void TXTextureAtlasSystem::BindAtlas(TXTextureAtlas* atlas)
{
    if (!atlas || atlas == m_CurrentAtlas)
        return;

    GPU_BindTexture(atlas->GPUHandle);
    m_CurrentAtlas = atlas;
}

// ------------------------------------------------------------
// Obtención de coordenadas UV normalizadas
// ------------------------------------------------------------

TXUVRect TXTextureAtlasSystem::GetUV(const TXTextureAtlas* atlas,
                                     const TXAtlasEntry& entry) const
{
    TXUVRect uv;

    uv.U0 = float(entry.X) / float(atlas->Width);
    uv.V0 = float(entry.Y) / float(atlas->Height);
    uv.U1 = float(entry.X + entry.W) / float(atlas->Width);
    uv.V1 = float(entry.Y + entry.H) / float(atlas->Height);

    return uv;
}

// ------------------------------------------------------------
// Limpieza de atlases no usados (PASS + LCR friendly)
// ------------------------------------------------------------

void TXTextureAtlasSystem::GarbageCollect()
{
    for (size_t i = 0; i < m_Atlases.size();)
    {
        TXTextureAtlas* atlas = m_Atlases[i];

        if (atlas->UsedEntries.empty())
        {
            ReleaseGPUTexture(atlas->GPUHandle);
            delete atlas;

            m_Atlases.erase(m_Atlases.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

// ------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------

void TXTextureAtlasSystem::Shutdown()
{
    for (TXTextureAtlas* atlas : m_Atlases)
    {
        ReleaseGPUTexture(atlas->GPUHandle);
        delete atlas;
    }

    m_Atlases.clear();
    m_CurrentAtlas = nullptr;

}
