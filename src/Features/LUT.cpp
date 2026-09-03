#include "LUT.h"

#include <cctype>
#include <d3d11.h>
#include <fstream>
#include <sstream>

namespace LUT
{
    namespace
    {
        const std::filesystem::path g_folder = "Data/Shaders/SimpleGraphicsSuite/LUTs";

        std::vector<std::string> g_names;

        std::string               g_loadedName;
        // Keeps a bad name from reopening the file on every Select.
        std::string               g_failedName;
        std::uint32_t              g_size = 0;
        ID3D11Texture3D*           g_texture = nullptr;
        ID3D11ShaderResourceView*  g_srv = nullptr;
        ID3D11SamplerState*        g_sampler = nullptr;

        void ReleaseTexture()
        {
            if (g_srv) {
                g_srv->Release();
                g_srv = nullptr;
            }
            if (g_texture) {
                g_texture->Release();
                g_texture = nullptr;
            }
            g_size = 0;
            g_loadedName.clear();
        }

        void EnsureSampler(ID3D11Device* a_device)
        {
            if (g_sampler)
                return;

            D3D11_SAMPLER_DESC desc{};
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.MaxLOD = D3D11_FLOAT32_MAX;

            if (FAILED(a_device->CreateSamplerState(&desc, &g_sampler)))
                logger::warn("LUT: couldn't create the sampler state");
        }

        // Standard .cube: "LUT_3D_SIZE N" then N^3 "R G B" triplets in
        // R-fastest order, which is already a Texture3D's expected layout.
        // TITLE/DOMAIN_MIN/MAX are skipped - only the plain 0-1 domain is
        // supported.
        bool ParseCube(const std::filesystem::path& a_path, std::uint32_t& a_outSize, std::vector<std::uint8_t>& a_outData)
        {
            std::ifstream file(a_path);
            if (!file)
                return false;

            std::uint32_t              size = 0;
            std::vector<std::uint8_t> data;

            std::string line;
            while (std::getline(file, line)) {
                const auto start = line.find_first_not_of(" \t\r");
                if (start == std::string::npos)
                    continue;
                line = line.substr(start);
                if (line.empty() || line[0] == '#')
                    continue;

                if (line.starts_with("LUT_3D_SIZE")) {
                    std::istringstream iss(line.substr(std::strlen("LUT_3D_SIZE")));
                    iss >> size;
                    if (size > 0)
                        data.reserve(static_cast<std::size_t>(size) * size * size * 4);
                    continue;
                }

                const auto first = static_cast<unsigned char>(line[0]);
                if (!std::isdigit(first) && line[0] != '-' && line[0] != '.')
                    continue;  // TITLE, DOMAIN_MIN/MAX, LUT_1D_SIZE, etc.

                std::istringstream iss(line);
                float              r, g, b;
                if (!(iss >> r >> g >> b))
                    continue;

                data.push_back(static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f));
                data.push_back(static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f));
                data.push_back(static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f));
                data.push_back(255);
            }

            if (size == 0 || data.size() != static_cast<std::size_t>(size) * size * size * 4) {
                logger::warn("LUT: {} - expected {}^3 RGB entries, got {} (malformed or unsupported .cube)",
                    a_path.filename().string(), size, data.size() / 4);
                return false;
            }

            a_outSize = size;
            a_outData = std::move(data);
            return true;
        }

        bool Load(const std::filesystem::path& a_path)
        {
            std::uint32_t              size = 0;
            std::vector<std::uint8_t> data;
            if (!ParseCube(a_path, size, data))
                return false;

            auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder);
            EnsureSampler(device);

            D3D11_TEXTURE3D_DESC desc{};
            desc.Width = desc.Height = desc.Depth = size;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = data.data();
            initData.SysMemPitch = size * 4;
            initData.SysMemSlicePitch = size * size * 4;

            ID3D11Texture3D* texture = nullptr;
            if (FAILED(device->CreateTexture3D(&desc, &initData, &texture))) {
                logger::warn("LUT: couldn't create the 3D texture for {}", a_path.filename().string());
                return false;
            }

            ID3D11ShaderResourceView* srv = nullptr;
            if (FAILED(device->CreateShaderResourceView(texture, nullptr, &srv))) {
                texture->Release();
                logger::warn("LUT: couldn't create the shader resource view for {}", a_path.filename().string());
                return false;
            }

            ReleaseTexture();
            g_texture = texture;
            g_srv = srv;
            g_size = size;
            g_loadedName = a_path.stem().string();
            g_failedName.clear();
            logger::info("LUT: loaded {} ({}x{}x{})", g_loadedName, size, size, size);
            return true;
        }
    }

    void Rescan()
    {
        g_names.clear();
        if (!std::filesystem::exists(g_folder))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(g_folder))
            if (entry.is_regular_file() && entry.path().extension() == ".cube")
                g_names.push_back(entry.path().stem().string());

        std::ranges::sort(g_names);
    }

    const std::vector<std::string>& AvailableNames() { return g_names; }

    bool Select(const std::string& a_name)
    {
        if (a_name.empty()) {
            ReleaseTexture();
            g_failedName.clear();
            return true;
        }
        if (g_loadedName == a_name && g_srv)
            return true;
        if (g_failedName == a_name)
            return false;

        if (Load(g_folder / (a_name + ".cube")))
            return true;

        g_failedName = a_name;
        return false;
    }

    ID3D11ShaderResourceView* CurrentSRV() { return g_srv; }
    ID3D11SamplerState*       Sampler() { return g_sampler; }
    std::uint32_t             CurrentSize() { return g_size; }
}
