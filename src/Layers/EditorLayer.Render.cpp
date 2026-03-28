#include "Layers/EditorLayerPrivate.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../vendor/glfw/deps/stb_image_write.h"

namespace ds
{
    void EditorLayer::DrawPeriodicTableWindow()
    {
        if (!m_PeriodicTableOpen)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Periodic Table", &m_PeriodicTableOpen))
        {
            ImGui::End();
            return;
        }

        const char *tableRows[7][18] = {
            {"H", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "He"},
            {"Li", "Be", "", "", "", "", "", "", "", "", "", "", "B", "C", "N", "O", "F", "Ne"},
            {"Na", "Mg", "", "", "", "", "", "", "", "", "", "", "Al", "Si", "P", "S", "Cl", "Ar"},
            {"K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr"},
            {"Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe"},
            {"Cs", "Ba", "", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn"},
            {"Fr", "Ra", "", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "", "", "", "", "", "", ""}};

        const char *lanthanoids[15] = {"La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu"};
        const char *actinoids[15] = {"Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr"};

        const float cellWidth = 36.0f;
        const float cellHeight = 32.0f;

        auto setSelectedElement = [&](const char *symbol)
        {
            if (m_PeriodicTableTarget == PeriodicTableTarget::ChangeSelectedAtoms)
            {
                std::snprintf(m_PendingChangeAtomElementBuffer.data(), m_PendingChangeAtomElementBuffer.size(), "%s", symbol);
                m_ChangeAtomTypeConfirmOpen = true;
            }
            else
            {
                std::snprintf(m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size(), "%s", symbol);
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Selected element: " + std::string(symbol);
                m_PeriodicTableOpenedFromContextMenu = false;
            }

            m_PeriodicTableOpen = false;
        };

        const std::string activeTargetElement =
            (m_PeriodicTableTarget == PeriodicTableTarget::ChangeSelectedAtoms)
                ? std::string(m_ChangeAtomElementBuffer.data())
                : std::string(m_AddAtomElementBuffer.data());

        for (int row = 0; row < 7; ++row)
        {
            for (int col = 0; col < 18; ++col)
            {
                if (col > 0)
                {
                    ImGui::SameLine();
                }

                const char *symbol = tableRows[row][col];
                if (symbol[0] == '\0')
                {
                    ImGui::Dummy(ImVec2(cellWidth, cellHeight));
                    continue;
                }

                const bool isSelected = activeTargetElement == symbol;
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
                }

                if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
                {
                    setSelectedElement(symbol);
                }

                if (isSelected)
                {
                    ImGui::PopStyleColor(3);
                }
            }
        }

        ImGui::Separator();

        ImGui::TextUnformatted("Lanthanoids");
        ImGui::SameLine();
        for (int i = 0; i < 15; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            const char *symbol = lanthanoids[i];
            if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
            {
                setSelectedElement(symbol);
            }
        }

        ImGui::TextUnformatted("Actinoids");
        ImGui::SameLine();
        for (int i = 0; i < 15; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            const char *symbol = actinoids[i];
            if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
            {
                setSelectedElement(symbol);
            }
        }

        ImGui::End();
    }

    void EditorLayer::DrawChangeAtomTypeConfirmDialog()
    {
        if (!m_ChangeAtomTypeConfirmOpen)
        {
            return;
        }

        ImGui::OpenPopup("Confirm atom type change");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Confirm atom type change", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("Change selected atoms to: %s", m_PendingChangeAtomElementBuffer.data());
        ImGui::TextDisabled("Enter: confirm   Esc: cancel and return to periodic table");
        ImGui::Separator();

        bool confirm = ImGui::Button("Confirm (Enter)");
        ImGui::SameLine();
        bool cancel = ImGui::Button("Back (Esc)");

        if (!confirm && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            confirm = true;
        }
        if (!cancel && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            cancel = true;
        }

        if (confirm)
        {
            std::size_t changedCount = 0;
            if (ApplyElementToSelectedAtoms(std::string(m_PendingChangeAtomElementBuffer.data()), &changedCount))
            {
                if (changedCount > 0)
                {
                    AppendSelectionDebugLog("Periodic table: changed selected atom type");
                }

                std::snprintf(m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size(), "%s", m_PendingChangeAtomElementBuffer.data());
                if (m_PeriodicTableOpenedFromContextMenu)
                {
                    m_ReopenViewportSelectionContextMenu = true;
                }
            }

            m_PeriodicTableOpenedFromContextMenu = false;
            m_ChangeAtomTypeConfirmOpen = false;
            ImGui::CloseCurrentPopup();
        }
        else if (cancel)
        {
            m_PeriodicTableOpen = true;
            m_PeriodicTableTarget = PeriodicTableTarget::ChangeSelectedAtoms;
            m_ChangeAtomTypeConfirmOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    std::vector<EditorLayer::BondLabelLayoutItem> EditorLayer::BuildBondLabelLayout(
        const OrbitCamera &camera,
        std::uint32_t sourceWidth,
        std::uint32_t sourceHeight,
        const glm::vec2 &targetRectMin,
        const glm::vec2 &targetRectMax,
        bool showLabels,
        float labelScaleMultiplier,
        int labelPrecision,
        bool useCrop,
        const std::array<float, 4> &cropRectNormalized) const
    {
        std::vector<BondLabelLayoutItem> layout;
        if (!showLabels || sourceWidth == 0 || sourceHeight == 0 || m_GeneratedBonds.empty())
        {
            return layout;
        }

        ImFont *font = ImGuiLayer::GetBondLabelFont();
        if (font == nullptr)
        {
            font = ImGui::GetFont();
        }
        if (font == nullptr)
        {
            return layout;
        }

        std::uint32_t cropX = 0;
        std::uint32_t cropY = 0;
        std::uint32_t cropWidth = sourceWidth;
        std::uint32_t cropHeight = sourceHeight;
        if (useCrop)
        {
            const float xNorm = glm::clamp(cropRectNormalized[0], 0.0f, 1.0f);
            const float yNorm = glm::clamp(cropRectNormalized[1], 0.0f, 1.0f);
            const float wNorm = glm::clamp(cropRectNormalized[2], 0.0f, 1.0f);
            const float hNorm = glm::clamp(cropRectNormalized[3], 0.0f, 1.0f);
            const float x1Norm = glm::clamp(xNorm + wNorm, 0.0f, 1.0f);
            const float y1Norm = glm::clamp(yNorm + hNorm, 0.0f, 1.0f);

            const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(xNorm * static_cast<float>(sourceWidth)));
            const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(yNorm * static_cast<float>(sourceHeight)));
            const std::uint32_t x1 = static_cast<std::uint32_t>(std::ceil(x1Norm * static_cast<float>(sourceWidth)));
            const std::uint32_t y1 = static_cast<std::uint32_t>(std::ceil(y1Norm * static_cast<float>(sourceHeight)));

            cropX = std::min(x0, sourceWidth - 1u);
            cropY = std::min(y0, sourceHeight - 1u);
            cropWidth = std::max(1u, std::min(sourceWidth, std::max(x1, cropX + 1u)) - cropX);
            cropHeight = std::max(1u, std::min(sourceHeight, std::max(y1, cropY + 1u)) - cropY);
        }

        const float targetWidth = std::max(1.0f, targetRectMax.x - targetRectMin.x);
        const float targetHeight = std::max(1.0f, targetRectMax.y - targetRectMin.y);
        const float scaleFactor = glm::clamp(targetHeight / 1080.0f, 0.45f, 8.0f);
        const float effectiveLabelScaleMultiplier = glm::clamp(labelScaleMultiplier, 0.25f, 4.0f);
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        const glm::vec3 cameraDirection = glm::normalize(glm::vec3(
            std::cos(camera.GetPitch()) * std::sin(camera.GetYaw()),
            std::cos(camera.GetPitch()) * std::cos(camera.GetYaw()),
            std::sin(camera.GetPitch())));
        const glm::vec3 cameraPosition = camera.GetTarget() - cameraDirection * camera.GetDistance();

        std::vector<std::pair<float, std::size_t>> labelOrder;
        labelOrder.reserve(m_GeneratedBonds.size());
        for (std::size_t i = 0; i < m_GeneratedBonds.size(); ++i)
        {
            const BondSegment &bond = m_GeneratedBonds[i];
            const std::uint64_t key = MakeBondPairKey(bond.atomA, bond.atomB);
            if (m_DeletedBondKeys.find(key) != m_DeletedBondKeys.end() ||
                m_HiddenBondKeys.find(key) != m_HiddenBondKeys.end() ||
                IsAtomHidden(bond.atomA) || IsAtomHidden(bond.atomB) ||
                !IsAtomCollectionVisible(bond.atomA) || !IsAtomCollectionVisible(bond.atomB))
            {
                continue;
            }

            labelOrder.emplace_back(glm::length2((bond.midpoint) - cameraPosition), i);
        }

        std::sort(labelOrder.begin(), labelOrder.end(), [](const auto &lhs, const auto &rhs)
                  { return lhs.first > rhs.first; });

        const int precision = std::clamp(labelPrecision, 0, 6);
        char formatSpec[16] = {};
        std::snprintf(formatSpec, sizeof(formatSpec), "%%.%df A", precision);
        layout.reserve(labelOrder.size());

        for (const auto &[depth, bondIndex] : labelOrder)
        {
            const BondSegment &bond = m_GeneratedBonds[bondIndex];
            const std::uint64_t key = MakeBondPairKey(bond.atomA, bond.atomB);
            const auto labelStateIt = m_BondLabelStates.find(key);
            if (labelStateIt != m_BondLabelStates.end() && (labelStateIt->second.hidden || labelStateIt->second.deleted))
            {
                continue;
            }

            const glm::vec3 labelWorld = bond.midpoint + ((labelStateIt != m_BondLabelStates.end()) ? labelStateIt->second.worldOffset : glm::vec3(0.0f));
            const glm::vec4 clip = viewProjection * glm::vec4(labelWorld, 1.0f);
            if (clip.w <= 0.0001f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
            {
                continue;
            }

            const float srcX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(sourceWidth);
            const float srcY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(sourceHeight);
            if (srcX < static_cast<float>(cropX) || srcX >= static_cast<float>(cropX + cropWidth) ||
                srcY < static_cast<float>(cropY) || srcY >= static_cast<float>(cropY + cropHeight))
            {
                continue;
            }

            char label[32] = {};
            std::snprintf(label, sizeof(label), formatSpec, bond.length);

            const float perLabelScale = (labelStateIt != m_BondLabelStates.end()) ? glm::clamp(labelStateIt->second.scale, 0.25f, 4.0f) : 1.0f;
            const float baseFontSize = std::max(1.0f, font->LegacySize);
            const float fontSize = std::max(10.0f, baseFontSize * scaleFactor * perLabelScale * effectiveLabelScaleMultiplier);
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), 0.0f, label);

            const float dstX = targetRectMin.x + ((srcX - static_cast<float>(cropX)) / static_cast<float>(cropWidth)) * targetWidth;
            const float dstY = targetRectMin.y + ((srcY - static_cast<float>(cropY)) / static_cast<float>(cropHeight)) * targetHeight;
            const float padding = std::max(3.0f, 4.0f * scaleFactor);

            BondLabelLayoutItem item;
            item.key = key;
            item.text = label;
            item.fontSize = fontSize;
            item.textSize = glm::vec2(textSize.x, textSize.y);
            item.textPos = glm::vec2(dstX - textSize.x * 0.5f, dstY - std::max(6.0f, 8.0f * scaleFactor) - textSize.y);
            item.boxMin = item.textPos - glm::vec2(padding, padding * 0.75f);
            item.boxMax = item.textPos + item.textSize + glm::vec2(padding, padding * 0.75f);
            item.depthToCamera = depth;
            item.selected = (m_SelectedBondLabelKey == key) || (m_SelectedBondKeys.find(key) != m_SelectedBondKeys.end());
            layout.push_back(std::move(item));
        }

        return layout;
    }

    bool EditorLayer::SaveCurrentFrameAsImage(
        const std::string &outputPath,
        std::uint32_t width,
        std::uint32_t height,
        RenderImageFormat format,
        bool useCrop,
        const std::array<float, 4> &cropRectNormalized) const
    {
        if (!m_RenderPreviewBackend || outputPath.empty())
        {
            return false;
        }

        std::uint32_t sourceWidth = 0;
        std::uint32_t sourceHeight = 0;
        std::vector<std::uint8_t> sourceRgba;
        if (!m_RenderPreviewBackend->ReadColorAttachmentPixels(sourceWidth, sourceHeight, sourceRgba))
        {
            return false;
        }

        if (sourceWidth == 0 || sourceHeight == 0)
        {
            return false;
        }

        // OpenGL textures start at bottom-left. Convert once to top-down before crop/scale.
        std::vector<std::uint8_t> sourceTopDown;
        sourceTopDown.resize(sourceRgba.size());
        const std::size_t sourceRowBytes = static_cast<std::size_t>(sourceWidth) * 4u;
        for (std::uint32_t row = 0; row < sourceHeight; ++row)
        {
            const std::size_t srcOffset = static_cast<std::size_t>(sourceHeight - 1u - row) * sourceRowBytes;
            const std::size_t dstOffset = static_cast<std::size_t>(row) * sourceRowBytes;
            std::memcpy(sourceTopDown.data() + dstOffset, sourceRgba.data() + srcOffset, sourceRowBytes);
        }

        std::uint32_t cropX = 0;
        std::uint32_t cropY = 0;
        std::uint32_t cropWidth = sourceWidth;
        std::uint32_t cropHeight = sourceHeight;
        if (useCrop)
        {
            const float xNorm = glm::clamp(cropRectNormalized[0], 0.0f, 1.0f);
            const float yNorm = glm::clamp(cropRectNormalized[1], 0.0f, 1.0f);
            const float wNorm = glm::clamp(cropRectNormalized[2], 0.0f, 1.0f);
            const float hNorm = glm::clamp(cropRectNormalized[3], 0.0f, 1.0f);

            const float x1Norm = glm::clamp(xNorm + wNorm, 0.0f, 1.0f);
            const float y1Norm = glm::clamp(yNorm + hNorm, 0.0f, 1.0f);

            const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(xNorm * static_cast<float>(sourceWidth)));
            const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(yNorm * static_cast<float>(sourceHeight)));
            const std::uint32_t x1 = static_cast<std::uint32_t>(std::ceil(x1Norm * static_cast<float>(sourceWidth)));
            const std::uint32_t y1 = static_cast<std::uint32_t>(std::ceil(y1Norm * static_cast<float>(sourceHeight)));

            cropX = std::min(x0, sourceWidth - 1u);
            cropY = std::min(y0, sourceHeight - 1u);
            cropWidth = std::max(1u, std::min(sourceWidth, std::max(x1, cropX + 1u)) - cropX);
            cropHeight = std::max(1u, std::min(sourceHeight, std::max(y1, cropY + 1u)) - cropY);
        }

        const std::uint32_t targetWidth = std::max(1u, width);
        const std::uint32_t targetHeight = std::max(1u, height);

        std::vector<std::uint8_t> scaledRgba;
        scaledRgba.resize(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4u);

        auto sampleSourceBilinear = [&](float srcX, float srcY, std::uint8_t *outRgba)
        {
            srcX = glm::clamp(srcX, 0.0f, static_cast<float>(sourceWidth - 1u));
            srcY = glm::clamp(srcY, 0.0f, static_cast<float>(sourceHeight - 1u));

            const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(srcX));
            const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(srcY));
            const std::uint32_t x1 = std::min(sourceWidth - 1u, x0 + 1u);
            const std::uint32_t y1 = std::min(sourceHeight - 1u, y0 + 1u);

            const float tx = srcX - static_cast<float>(x0);
            const float ty = srcY - static_cast<float>(y0);

            const std::size_t i00 = (static_cast<std::size_t>(y0) * sourceWidth + x0) * 4u;
            const std::size_t i10 = (static_cast<std::size_t>(y0) * sourceWidth + x1) * 4u;
            const std::size_t i01 = (static_cast<std::size_t>(y1) * sourceWidth + x0) * 4u;
            const std::size_t i11 = (static_cast<std::size_t>(y1) * sourceWidth + x1) * 4u;

            for (int c = 0; c < 4; ++c)
            {
                const float v00 = static_cast<float>(sourceTopDown[i00 + static_cast<std::size_t>(c)]);
                const float v10 = static_cast<float>(sourceTopDown[i10 + static_cast<std::size_t>(c)]);
                const float v01 = static_cast<float>(sourceTopDown[i01 + static_cast<std::size_t>(c)]);
                const float v11 = static_cast<float>(sourceTopDown[i11 + static_cast<std::size_t>(c)]);
                const float vx0 = v00 + (v10 - v00) * tx;
                const float vx1 = v01 + (v11 - v01) * tx;
                const float vxy = vx0 + (vx1 - vx0) * ty;
                outRgba[c] = static_cast<std::uint8_t>(glm::clamp(vxy, 0.0f, 255.0f));
            }
        };

        for (std::uint32_t y = 0; y < targetHeight; ++y)
        {
            for (std::uint32_t x = 0; x < targetWidth; ++x)
            {
                const float srcX = static_cast<float>(cropX) +
                                   ((static_cast<float>(x) + 0.5f) * static_cast<float>(cropWidth) / static_cast<float>(targetWidth)) - 0.5f;
                const float srcY = static_cast<float>(cropY) +
                                   ((static_cast<float>(y) + 0.5f) * static_cast<float>(cropHeight) / static_cast<float>(targetHeight)) - 0.5f;

                const std::size_t dstIndex = (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
                sampleSourceBilinear(srcX, srcY, &scaledRgba[dstIndex]);
            }
        }

        OrbitCamera exportCamera = *m_Camera;
        exportCamera.SetViewportSize(static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));

        auto drawFilledRect = [&](int x0, int y0, int x1, int y1, const glm::u8vec4 &color)
        {
            x0 = std::max(0, x0);
            y0 = std::max(0, y0);
            x1 = std::min(static_cast<int>(targetWidth), x1);
            y1 = std::min(static_cast<int>(targetHeight), y1);
            if (x0 >= x1 || y0 >= y1)
            {
                return;
            }

            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const std::size_t idx = (static_cast<std::size_t>(y) * targetWidth + static_cast<std::size_t>(x)) * 4u;
                    scaledRgba[idx + 0] = color.r;
                    scaledRgba[idx + 1] = color.g;
                    scaledRgba[idx + 2] = color.b;
                    scaledRgba[idx + 3] = color.a;
                }
            }
        };

        auto blendPixel = [&](int x, int y, const glm::u8vec4 &src)
        {
            if (x < 0 || y < 0 || x >= static_cast<int>(targetWidth) || y >= static_cast<int>(targetHeight) || src.a == 0u)
            {
                return;
            }

            const std::size_t idx = (static_cast<std::size_t>(y) * targetWidth + static_cast<std::size_t>(x)) * 4u;
            const float srcAlpha = static_cast<float>(src.a) / 255.0f;
            const float dstAlpha = static_cast<float>(scaledRgba[idx + 3]) / 255.0f;
            const float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
            auto blendChannel = [&](int channel) -> std::uint8_t
            {
                const float srcValue = static_cast<float>(src[channel]) / 255.0f;
                const float dstValue = static_cast<float>(scaledRgba[idx + channel]) / 255.0f;
                const float outValue = (outAlpha > 1e-6f) ? ((srcValue * srcAlpha) + (dstValue * dstAlpha * (1.0f - srcAlpha))) / outAlpha : 0.0f;
                return static_cast<std::uint8_t>(glm::clamp(outValue * 255.0f, 0.0f, 255.0f));
            };

            scaledRgba[idx + 0] = blendChannel(0);
            scaledRgba[idx + 1] = blendChannel(1);
            scaledRgba[idx + 2] = blendChannel(2);
            scaledRgba[idx + 3] = static_cast<std::uint8_t>(glm::clamp(outAlpha * 255.0f, 0.0f, 255.0f));
        };

        auto drawTextToImage = [&](const BondLabelLayoutItem &item, const glm::u8vec4 &textColor)
        {
            ImFont *font = ImGuiLayer::GetBondLabelFont();
            if (font == nullptr)
            {
                font = ImGui::GetFont();
            }
            if (font == nullptr || font->OwnerAtlas == nullptr)
            {
                return;
            }

            ImFontBaked *bakedFont = font->GetFontBaked(item.fontSize);
            if (bakedFont == nullptr)
            {
                return;
            }

            unsigned char *atlasPixels = nullptr;
            int atlasWidth = 0;
            int atlasHeight = 0;
            font->OwnerAtlas->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
            if (atlasPixels == nullptr || atlasWidth <= 0 || atlasHeight <= 0)
            {
                return;
            }

            ImVec2 pen(item.textPos.x, item.textPos.y);
            for (char c : item.text)
            {
                const ImFontGlyph *glyph = bakedFont->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(c)));
                if (glyph == nullptr)
                {
                    pen.x += bakedFont->FallbackAdvanceX;
                    continue;
                }

                const int glyphWidth = std::max(1, static_cast<int>(std::round(glyph->X1 - glyph->X0)));
                const int glyphHeight = std::max(1, static_cast<int>(std::round(glyph->Y1 - glyph->Y0)));
                const float glyphX = pen.x + glyph->X0;
                const float glyphY = pen.y + glyph->Y0;
                const float atlasX0 = glyph->U0 * static_cast<float>(atlasWidth);
                const float atlasY0 = glyph->V0 * static_cast<float>(atlasHeight);
                const float atlasX1 = glyph->U1 * static_cast<float>(atlasWidth);
                const float atlasY1 = glyph->V1 * static_cast<float>(atlasHeight);

                for (int py = 0; py < glyphHeight; ++py)
                {
                    const float sampleY = atlasY0 + ((static_cast<float>(py) + 0.5f) / static_cast<float>(glyphHeight)) * (atlasY1 - atlasY0);
                    const int atlasYi = glm::clamp(static_cast<int>(sampleY), 0, atlasHeight - 1);
                    for (int px = 0; px < glyphWidth; ++px)
                    {
                        const float sampleX = atlasX0 + ((static_cast<float>(px) + 0.5f) / static_cast<float>(glyphWidth)) * (atlasX1 - atlasX0);
                        const int atlasXi = glm::clamp(static_cast<int>(sampleX), 0, atlasWidth - 1);
                        const std::size_t atlasIndex = (static_cast<std::size_t>(atlasYi) * static_cast<std::size_t>(atlasWidth) + static_cast<std::size_t>(atlasXi)) * 4u;
                        const std::uint8_t glyphAlpha = atlasPixels[atlasIndex + 3];
                        if (glyphAlpha == 0u)
                        {
                            continue;
                        }

                        glm::u8vec4 src = textColor;
                        src.a = static_cast<std::uint8_t>((static_cast<std::uint32_t>(src.a) * static_cast<std::uint32_t>(glyphAlpha)) / 255u);
                        blendPixel(
                            static_cast<int>(std::round(glyphX)) + px,
                            static_cast<int>(std::round(glyphY)) + py,
                            src);
                    }
                }

                pen.x += glyph->AdvanceX;
            }
        };

        const auto labelItems = BuildBondLabelLayout(
            exportCamera,
            sourceWidth,
            sourceHeight,
            glm::vec2(0.0f),
            glm::vec2(static_cast<float>(targetWidth), static_cast<float>(targetHeight)),
            m_RenderImageRequest.showBondLengthLabels,
            m_RenderImageRequest.bondLabelScaleMultiplier,
            m_RenderImageRequest.bondLabelPrecision,
            useCrop,
            cropRectNormalized);

        const glm::vec3 finalTextColor = glm::clamp(m_RenderImageRequest.bondLabelTextColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 finalBgColor = glm::clamp(m_RenderImageRequest.bondLabelBackgroundColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 finalBorderColor = glm::clamp(m_RenderImageRequest.bondLabelBorderColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 selectedBgColor = glm::clamp(finalBgColor + glm::vec3(0.18f, 0.20f, 0.24f), glm::vec3(0.0f), glm::vec3(1.0f));

        const glm::u8vec4 textColorNormal(
            static_cast<std::uint8_t>(finalTextColor.r * 255.0f),
            static_cast<std::uint8_t>(finalTextColor.g * 255.0f),
            static_cast<std::uint8_t>(finalTextColor.b * 255.0f),
            245u);
        const glm::u8vec4 textColorSelected(255u, 237u, 179u, 250u);
        const glm::u8vec4 bgColorNormal(
            static_cast<std::uint8_t>(finalBgColor.r * 255.0f),
            static_cast<std::uint8_t>(finalBgColor.g * 255.0f),
            static_cast<std::uint8_t>(finalBgColor.b * 255.0f),
            188u);
        const glm::u8vec4 bgColorSelected(
            static_cast<std::uint8_t>(selectedBgColor.r * 255.0f),
            static_cast<std::uint8_t>(selectedBgColor.g * 255.0f),
            static_cast<std::uint8_t>(selectedBgColor.b * 255.0f),
            220u);
        const glm::u8vec4 borderColor(
            static_cast<std::uint8_t>(finalBorderColor.r * 255.0f),
            static_cast<std::uint8_t>(finalBorderColor.g * 255.0f),
            static_cast<std::uint8_t>(finalBorderColor.b * 255.0f),
            235u);

        for (const BondLabelLayoutItem &item : labelItems)
        {
            const glm::u8vec4 bgColor = item.selected ? bgColorSelected : bgColorNormal;
            const glm::u8vec4 textColor = item.selected ? textColorSelected : textColorNormal;
            drawFilledRect(
                static_cast<int>(std::floor(item.boxMin.x)),
                static_cast<int>(std::floor(item.boxMin.y)),
                static_cast<int>(std::ceil(item.boxMax.x)),
                static_cast<int>(std::ceil(item.boxMax.y)),
                bgColor);
            drawFilledRect(static_cast<int>(std::floor(item.boxMin.x)), static_cast<int>(std::floor(item.boxMin.y)), static_cast<int>(std::ceil(item.boxMax.x)), static_cast<int>(std::floor(item.boxMin.y)) + 1, borderColor);
            drawFilledRect(static_cast<int>(std::floor(item.boxMin.x)), static_cast<int>(std::ceil(item.boxMax.y)) - 1, static_cast<int>(std::ceil(item.boxMax.x)), static_cast<int>(std::ceil(item.boxMax.y)), borderColor);
            drawFilledRect(static_cast<int>(std::floor(item.boxMin.x)), static_cast<int>(std::floor(item.boxMin.y)), static_cast<int>(std::floor(item.boxMin.x)) + 1, static_cast<int>(std::ceil(item.boxMax.y)), borderColor);
            drawFilledRect(static_cast<int>(std::ceil(item.boxMax.x)) - 1, static_cast<int>(std::floor(item.boxMin.y)), static_cast<int>(std::ceil(item.boxMax.x)), static_cast<int>(std::ceil(item.boxMax.y)), borderColor);
            drawTextToImage(item, textColor);
        }

        std::filesystem::path output = outputPath;
        std::filesystem::create_directories(output.parent_path());

        const std::size_t rowBytes = static_cast<std::size_t>(targetWidth) * 4u;

        if (format == RenderImageFormat::Png)
        {
            const int ok = stbi_write_png(output.string().c_str(),
                                          static_cast<int>(targetWidth),
                                          static_cast<int>(targetHeight),
                                          4,
                                          scaledRgba.data(),
                                          static_cast<int>(rowBytes));
            return ok != 0;
        }

        std::vector<std::uint8_t> scaledRgb;
        scaledRgb.resize(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 3u);
        for (std::size_t i = 0, j = 0; i < scaledRgba.size(); i += 4, j += 3)
        {
            scaledRgb[j + 0] = scaledRgba[i + 0];
            scaledRgb[j + 1] = scaledRgba[i + 1];
            scaledRgb[j + 2] = scaledRgba[i + 2];
        }

        const int quality = std::clamp(m_RenderJpegQuality, 20, 100);
        const int ok = stbi_write_jpg(output.string().c_str(),
                                      static_cast<int>(targetWidth),
                                      static_cast<int>(targetHeight),
                                      3,
                                      scaledRgb.data(),
                                      quality);
        return ok != 0;
    }

    void EditorLayer::DrawRenderPreviewWindow(bool &settingsChanged)
    {
        if (!m_ShowRenderPreviewWindow)
        {
            return;
        }

        bool previewOpen = m_ShowRenderPreviewWindow;
        ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Render Preview", &previewOpen))
        {
            ImGui::End();
            m_ShowRenderPreviewWindow = previewOpen;
            return;
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_RenderPreviewContentSize = glm::vec2(std::max(avail.x, 240.0f), std::max(avail.y, 160.0f));
        ImGui::Text("Target: %dx%d", m_RenderImageWidth, m_RenderImageHeight);
        ImGui::SameLine();
        ImGui::TextDisabled("Preview RT: %ux%u", m_RenderPreviewTargetWidth, m_RenderPreviewTargetHeight);

        if (!m_RenderPreviewBackend)
        {
            ImGui::TextUnformatted("Preview backend not available.");
            ImGui::End();
            m_ShowRenderPreviewWindow = previewOpen;
            return;
        }

        const std::uint32_t texture = m_RenderPreviewBackend->GetColorAttachmentRendererID();
        if (texture == 0 || m_RenderPreviewTargetWidth == 0 || m_RenderPreviewTargetHeight == 0)
        {
            ImGui::TextUnformatted("Preview render target not ready.");
            ImGui::End();
            m_ShowRenderPreviewWindow = previewOpen;
            return;
        }

        const float logicalAspect = static_cast<float>(std::max(1, m_RenderImageWidth)) / static_cast<float>(std::max(1, m_RenderImageHeight));
        ImVec2 imageSize(avail.x, avail.x / std::max(0.01f, logicalAspect));
        if (imageSize.y > avail.y)
        {
            imageSize.y = avail.y;
            imageSize.x = imageSize.y * logicalAspect;
        }
        imageSize.x = std::max(1.0f, imageSize.x);
        imageSize.y = std::max(1.0f, imageSize.y);

        ImVec2 uv0(0.0f, 1.0f);
        ImVec2 uv1(1.0f, 0.0f);
        if (m_RenderCropEnabled)
        {
            const float x = glm::clamp(m_RenderCropRectNormalized[0], 0.0f, 1.0f);
            const float y = glm::clamp(m_RenderCropRectNormalized[1], 0.0f, 1.0f);
            const float w = glm::clamp(m_RenderCropRectNormalized[2], 0.01f, 1.0f);
            const float h = glm::clamp(m_RenderCropRectNormalized[3], 0.01f, 1.0f);
            uv0 = ImVec2(x, 1.0f - y);
            uv1 = ImVec2(x + w, 1.0f - (y + h));
        }

        ImTextureID textureID = (ImTextureID)(std::uintptr_t)texture;
        ImGui::Image(textureID, imageSize, uv0, uv1);
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();

        OrbitCamera previewCamera = *m_Camera;
        previewCamera.SetViewportSize(static_cast<float>(m_RenderPreviewTargetWidth), static_cast<float>(m_RenderPreviewTargetHeight));
        const auto labelItems = BuildBondLabelLayout(
            previewCamera,
            m_RenderPreviewTargetWidth,
            m_RenderPreviewTargetHeight,
            glm::vec2(imageMin.x, imageMin.y),
            glm::vec2(imageMax.x, imageMax.y),
            m_RenderShowBondLengthLabels,
            m_RenderBondLabelScaleMultiplier,
            m_RenderBondLabelPrecision,
            m_RenderCropEnabled,
            m_RenderCropRectNormalized);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const glm::vec3 finalTextColor = glm::clamp(m_RenderBondLabelTextColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 finalBgColor = glm::clamp(m_RenderBondLabelBackgroundColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 finalBorderColor = glm::clamp(m_RenderBondLabelBorderColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const glm::vec3 selectedBgColor = glm::clamp(finalBgColor + glm::vec3(0.18f, 0.20f, 0.24f), glm::vec3(0.0f), glm::vec3(1.0f));
        const ImU32 textColorNormal = ImGui::ColorConvertFloat4ToU32(ImVec4(finalTextColor.r, finalTextColor.g, finalTextColor.b, 0.96f));
        const ImU32 textColorSelected = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.93f, 0.70f, 0.98f));
        const ImU32 bgColorNormal = ImGui::ColorConvertFloat4ToU32(ImVec4(finalBgColor.r, finalBgColor.g, finalBgColor.b, 0.74f));
        const ImU32 bgColorSelected = ImGui::ColorConvertFloat4ToU32(ImVec4(selectedBgColor.r, selectedBgColor.g, selectedBgColor.b, 0.86f));
        const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(finalBorderColor.r, finalBorderColor.g, finalBorderColor.b, 0.92f));
        ImFont *font = ImGuiLayer::GetBondLabelFont();
        if (font == nullptr)
        {
            font = ImGui::GetFont();
        }

        for (const BondLabelLayoutItem &item : labelItems)
        {
            drawList->AddRectFilled(
                ImVec2(item.boxMin.x, item.boxMin.y),
                ImVec2(item.boxMax.x, item.boxMax.y),
                item.selected ? bgColorSelected : bgColorNormal,
                2.0f);
            drawList->AddRect(
                ImVec2(item.boxMin.x, item.boxMin.y),
                ImVec2(item.boxMax.x, item.boxMax.y),
                borderColor,
                2.0f,
                0,
                item.selected ? 1.6f : 1.0f);
            drawList->AddText(
                font,
                item.fontSize,
                ImVec2(item.textPos.x, item.textPos.y),
                item.selected ? textColorSelected : textColorNormal,
                item.text.c_str());
        }

        ImGui::End();
        m_ShowRenderPreviewWindow = previewOpen;
        (void)settingsChanged;
    }

    void EditorLayer::DrawRenderImageDialog(bool &settingsChanged)
    {
        if (!m_ShowRenderImageDialog)
        {
            return;
        }

        bool dialogOpen = m_ShowRenderImageDialog;
        ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Render Image", &dialogOpen))
        {
            const ImGuiTreeNodeFlags defaultOpenFlags = ImGuiTreeNodeFlags_DefaultOpen;
            const float nestedSectionIndent = ImGui::GetStyle().IndentSpacing * 0.65f;
            auto ensureFloatRange = [](float &minValue, float &maxValue, float hardMin, float hardMax)
            {
                minValue = glm::clamp(minValue, hardMin, hardMax);
                maxValue = glm::clamp(maxValue, hardMin, hardMax);
                if (minValue > maxValue)
                {
                    std::swap(minValue, maxValue);
                }
            };
            auto ensureIntRange = [](int &minValue, int &maxValue, int hardMin, int hardMax)
            {
                minValue = std::clamp(minValue, hardMin, hardMax);
                maxValue = std::clamp(maxValue, hardMin, hardMax);
                if (minValue > maxValue)
                {
                    std::swap(minValue, maxValue);
                }
            };

            ImGui::InputText("Output path", m_RenderImagePathBuffer.data(), m_RenderImagePathBuffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                std::string selectedPath;
                if (SaveNativeImageDialog(selectedPath))
                {
                    std::snprintf(m_RenderImagePathBuffer.data(), m_RenderImagePathBuffer.size(), "%s", selectedPath.c_str());
                }
            }

            int width = std::max(1, m_RenderImageWidth);
            int height = std::max(1, m_RenderImageHeight);
            if (ImGui::InputInt("Width", &width, 64, 256))
            {
                m_RenderImageWidth = std::clamp(width, 64, 8192);
                settingsChanged = true;
            }
            if (ImGui::InputInt("Height", &height, 64, 256))
            {
                m_RenderImageHeight = std::clamp(height, 64, 8192);
                settingsChanged = true;
            }

            ImGui::TextUnformatted("Quick presets:");
            struct ResolutionPreset
            {
                const char *label;
                int width;
                int height;
            };
            static constexpr ResolutionPreset kResolutionPresets[] = {
                {"HD", 1280, 720},
                {"FHD", 1920, 1080},
                {"QHD", 2560, 1440},
                {"4K UHD", 3840, 2160},
                {"8K UHD", 7680, 4320}};
            for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kResolutionPresets)); ++i)
            {
                if (ImGui::Button(kResolutionPresets[i].label))
                {
                    m_RenderImageWidth = kResolutionPresets[i].width;
                    m_RenderImageHeight = kResolutionPresets[i].height;
                    settingsChanged = true;
                }
                if (i + 1 < static_cast<int>(IM_ARRAYSIZE(kResolutionPresets)))
                {
                    ImGui::SameLine();
                }
            }

            const char *formatLabels[] = {"PNG", "JPG"};
            int formatIndex = static_cast<int>(m_RenderImageFormat);
            if (ImGui::Combo("Format", &formatIndex, formatLabels, IM_ARRAYSIZE(formatLabels)))
            {
                m_RenderImageFormat = static_cast<RenderImageFormat>(std::clamp(formatIndex, 0, 1));
                settingsChanged = true;
            }

            if (m_RenderImageFormat == RenderImageFormat::Jpg)
            {
                if (ImGui::SliderInt("JPG quality", &m_RenderJpegQuality, 20, 100))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::Checkbox("Live preview window", &m_ShowRenderPreviewWindow))
            {
                settingsChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy current viewport look"))
            {
                SyncRenderAppearanceFromViewport();
                settingsChanged = true;
            }

            if (ImGui::CollapsingHeader("Grid & Background", defaultOpenFlags))
            {
                ensureIntRange(m_GridHalfExtentMin, m_GridHalfExtentMax, 1, 128);
                ensureFloatRange(m_GridSpacingMin, m_GridSpacingMax, 0.01f, 20.0f);
                ensureFloatRange(m_GridLineWidthMin, m_GridLineWidthMax, 0.5f, 10.0f);
                ensureFloatRange(m_GridOpacityMin, m_GridOpacityMax, 0.01f, 1.0f);

                if (ImGui::ColorEdit3("Render background", &m_RenderSceneSettings.clearColor.x))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("White background preset"))
                {
                    m_RenderSceneSettings.clearColor = glm::vec3(1.0f);
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Draw grid", &m_RenderSceneSettings.drawGrid))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Draw cell edges", &m_RenderSceneSettings.drawCellEdges))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderInt("Grid half extent", &m_RenderSceneSettings.gridHalfExtent, m_GridHalfExtentMin, m_GridHalfExtentMax))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid spacing", &m_RenderSceneSettings.gridSpacing, m_GridSpacingMin, m_GridSpacingMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid line width", &m_RenderSceneSettings.gridLineWidth, m_GridLineWidthMin, m_GridLineWidthMax, "%.1f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Grid color", &m_RenderSceneSettings.gridColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid opacity", &m_RenderSceneSettings.gridOpacity, m_GridOpacityMin, m_GridOpacityMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Cell edge color", &m_RenderSceneSettings.cellEdgeColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Cell edge width", &m_RenderSceneSettings.cellEdgeLineWidth, 0.5f, 10.0f, "%.1f"))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Lighting", defaultOpenFlags))
            {
                ensureFloatRange(m_AmbientMin, m_AmbientMax, 0.0f, 4.0f);
                ensureFloatRange(m_DiffuseMin, m_DiffuseMax, 0.0f, 4.0f);

                if (ImGui::SliderFloat3("Light direction", &m_RenderSceneSettings.lightDirection.x, -1.0f, 1.0f, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Ambient", &m_RenderSceneSettings.ambientStrength, m_AmbientMin, m_AmbientMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Diffuse", &m_RenderSceneSettings.diffuseStrength, m_DiffuseMin, m_DiffuseMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Light color", &m_RenderSceneSettings.lightColor.x))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Atoms", defaultOpenFlags))
            {
                ImGui::Indent(nestedSectionIndent);
                if (ImGui::CollapsingHeader("Rendering", defaultOpenFlags))
                {
                    ensureFloatRange(m_AtomSizeMin, m_AtomSizeMax, 0.01f, 4.0f);
                    ensureFloatRange(m_AtomBrightnessMin, m_AtomBrightnessMax, 0.05f, 6.0f);
                    ensureFloatRange(m_AtomGlowMin, m_AtomGlowMax, 0.0f, 2.0f);

                    if (ImGui::SliderFloat("Atom size", &m_RenderSceneSettings.atomScale, m_AtomSizeMin, m_AtomSizeMax, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::SliderFloat("Atom brightness", &m_RenderSceneSettings.atomBrightness, m_AtomBrightnessMin, m_AtomBrightnessMax, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::SliderFloat("Atom glow", &m_RenderSceneSettings.atomGlowStrength, m_AtomGlowMin, m_AtomGlowMax, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::Checkbox("Override atom color", &m_RenderSceneSettings.overrideAtomColor))
                    {
                        settingsChanged = true;
                    }
                    if (m_RenderSceneSettings.overrideAtomColor &&
                        ImGui::ColorEdit3("Atom color", &m_RenderSceneSettings.atomOverrideColor.x,
                                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                    {
                        settingsChanged = true;
                    }
                }
                ImGui::Unindent(nestedSectionIndent);
            }

            if (ImGui::CollapsingHeader("Overlays", defaultOpenFlags))
            {
                constexpr ImGuiColorEditFlags kPickerOnlyFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;
                if (ImGui::Checkbox("Show bond length labels", &m_RenderShowBondLengthLabels))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderInt("Bond label precision", &m_RenderBondLabelPrecision, 0, 6))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Bond label scale", &m_RenderBondLabelScaleMultiplier, 0.25f, 4.0f, "%.2f"))
                {
                    m_RenderBondLabelScaleMultiplier = glm::clamp(m_RenderBondLabelScaleMultiplier, 0.25f, 4.0f);
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Bond label text", &m_RenderBondLabelTextColor.x, kPickerOnlyFlags))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Bond label frame", &m_RenderBondLabelBorderColor.x, kPickerOnlyFlags))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Bond label background", &m_RenderBondLabelBackgroundColor.x, kPickerOnlyFlags))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Capture area", defaultOpenFlags))
            {
                if (ImGui::Checkbox("Crop to selected area", &m_RenderCropEnabled))
                {
                    settingsChanged = true;
                }
                if (m_RenderCropEnabled)
                {
                    float x = glm::clamp(m_RenderCropRectNormalized[0], 0.0f, 1.0f);
                    float y = glm::clamp(m_RenderCropRectNormalized[1], 0.0f, 1.0f);
                    float w = glm::clamp(m_RenderCropRectNormalized[2], 0.01f, 1.0f);
                    float h = glm::clamp(m_RenderCropRectNormalized[3], 0.01f, 1.0f);

                    bool cropChanged = false;
                    cropChanged |= ImGui::SliderFloat("Crop X", &x, 0.0f, 1.0f, "%.3f");
                    cropChanged |= ImGui::SliderFloat("Crop Y", &y, 0.0f, 1.0f, "%.3f");
                    cropChanged |= ImGui::SliderFloat("Crop Width", &w, 0.01f, 1.0f, "%.3f");
                    cropChanged |= ImGui::SliderFloat("Crop Height", &h, 0.01f, 1.0f, "%.3f");

                    if (cropChanged)
                    {
                        x = glm::clamp(x, 0.0f, 1.0f);
                        y = glm::clamp(y, 0.0f, 1.0f);
                        const float maxWidth = std::max(0.01f, 1.0f - x);
                        const float maxHeight = std::max(0.01f, 1.0f - y);
                        w = glm::clamp(w, 0.01f, maxWidth);
                        h = glm::clamp(h, 0.01f, maxHeight);
                        m_RenderCropRectNormalized = {x, y, w, h};
                        settingsChanged = true;
                    }

                    if (ImGui::Button("Reset crop area"))
                    {
                        m_RenderCropRectNormalized = {0.0f, 0.0f, 1.0f, 1.0f};
                        settingsChanged = true;
                    }
                }
            }

            if (ImGui::CollapsingHeader("Preview", defaultOpenFlags))
            {
                ImGui::TextUnformatted("Render Preview can now dock with this window.");
                ImGui::TextDisabled("Drag the Render Preview tab onto this window to dock it.");
            }

            ImGui::Separator();
            if (ImGui::Button("Render and save"))
            {
                const std::string outputPath = m_RenderImagePathBuffer.data();
                if (outputPath.empty())
                {
                    m_LastStructureOperationFailed = true;
                    m_LastStructureMessage = "Render image export failed: output path is empty.";
                    LogError(m_LastStructureMessage);
                }
                else
                {
                    m_RenderImageRequest.outputPath = outputPath;
                    m_RenderImageRequest.width = static_cast<std::uint32_t>(std::max(1, m_RenderImageWidth));
                    m_RenderImageRequest.height = static_cast<std::uint32_t>(std::max(1, m_RenderImageHeight));
                    m_RenderImageRequest.format = m_RenderImageFormat;
                    m_RenderImageRequest.sceneSettings = m_RenderSceneSettings;
                    m_RenderImageRequest.sceneSettings.gridOrigin = m_SceneSettings.gridOrigin;
                    m_RenderImageRequest.showBondLengthLabels = m_RenderShowBondLengthLabels;
                    m_RenderImageRequest.bondLabelScaleMultiplier = m_RenderBondLabelScaleMultiplier;
                    m_RenderImageRequest.bondLabelPrecision = m_RenderBondLabelPrecision;
                    m_RenderImageRequest.bondLabelTextColor = glm::clamp(m_RenderBondLabelTextColor, glm::vec3(0.0f), glm::vec3(1.0f));
                    m_RenderImageRequest.bondLabelBackgroundColor = glm::clamp(m_RenderBondLabelBackgroundColor, glm::vec3(0.0f), glm::vec3(1.0f));
                    m_RenderImageRequest.bondLabelBorderColor = glm::clamp(m_RenderBondLabelBorderColor, glm::vec3(0.0f), glm::vec3(1.0f));
                    m_RenderImageRequest.useCrop = m_RenderCropEnabled;
                    m_RenderImageRequest.cropRectNormalized = m_RenderCropRectNormalized;

                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Render request queued: " + outputPath;
                    LogInfo(m_LastStructureMessage);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                dialogOpen = false;
            }
        }
        ImGui::End();

        m_ShowRenderImageDialog = dialogOpen;
    }


} // namespace ds
