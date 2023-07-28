#include "PlacementTool.h"
#include "chisel/Chisel.h"
#include "chisel/MapRender.h"
#include "gui/Viewport.h"
#include "gui/IconsMaterialCommunity.h"
#include "gui/Inspector.h"

namespace chisel
{
    struct EntityTool final : public PlacementTool
    {
        EntityTool() : PlacementTool("Entity", ICON_MC_LIGHTBULB, 100) {}
        virtual bool HasPropertiesGUI() { return true; }
        virtual void DrawPropertiesGUI();
        virtual void OnRayHit(Viewport& viewport);

        std::string className = "info_player_start";
        bool        randomYaw = false;
    };

    static EntityTool Instance;

    void EntityTool::DrawPropertiesGUI()
    {
        // TODO: Prefabs & instances mode
        Inspector::ClassnamePicker(&className, false, "Entity Type");
        ImGui::Checkbox("Random Yaw", &randomYaw);
    }

    void EntityTool::OnRayHit(Viewport& viewport)
    {
        // Draw hypothetical entity
        Chisel.Renderer->DrawPointEntity(className, true, point);

        // Place entity on click
        if (viewport.mouseOver && Mouse.GetButtonDown(MouseButton::Left))
        {
            PointEntity* pt = Chisel.map.AddPointEntity(className.c_str());
            pt->origin = point;
            Selection.Clear();
            Selection.Select(pt);
        }
    }
}