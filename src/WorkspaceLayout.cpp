#include "Interface.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

struct WorkspaceLayout {
  const char *name;
  const char *filename;
};

constexpr std::array workspaceLayouts{
    WorkspaceLayout{"Default", "default.ini"},
    WorkspaceLayout{"Side by Side", "side-by-side.ini"}};

} // namespace

void loadWorkspaceLayout(Interface &interface) {
  const auto path = std::filesystem::path(LAYOUT_DIRECTORY) /
                    workspaceLayouts.at(interface.requestedLayout).filename;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open layout: " + path.string());
  }
  const std::string settings{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};
  if (file.bad() || settings.empty()) {
    throw std::runtime_error("Cannot read layout: " + path.string());
  }

  // Replace all settings so a previous preset cannot leave stale docking state.
  ImGui::ClearIniSettings();
  ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
  interface.selectedLayout = interface.requestedLayout;
  interface.layoutChangeRequested = false;
}

void drawWorkspaceLayoutSelector(Interface &interface) {
  if (ImGui::BeginCombo("Layout", workspaceLayouts[interface.selectedLayout].name)) {
    for (size_t index = 0; index < workspaceLayouts.size(); ++index) {
      const bool selected = interface.selectedLayout == index;
      if (ImGui::Selectable(workspaceLayouts[index].name, selected)) {
        interface.requestedLayout = index;
        interface.layoutChangeRequested = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("Built-in layouts are locked");
}

void beginWorkspaceDockspace() {
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  const ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  ImGui::Begin("Blossom Workspace", nullptr, hostFlags);
  ImGui::PopStyleVar(2);
  const ImGuiID dockspace = ImGui::GetID("BlossomDockspace");
  const ImGuiDockNodeFlags dockFlags =
      static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoResize) |
      ImGuiDockNodeFlags_NoDocking | ImGuiDockNodeFlags_NoWindowMenuButton;
  ImGui::DockSpace(dockspace, {0.0f, 0.0f}, dockFlags);
  ImGui::End();
}
