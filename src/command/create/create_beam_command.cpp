#include "command/create/create_beam_command.h"

#include "command/create/beam_drag.h"
#include "entity/family/host/structural/beam_entity.h"

#include <string>

namespace tamias {

CreateBeamCommand::CreateBeamCommand(Document& document, double width, double depth,
                                     double elevation)
    : document_(&document),
      width_(width),
      depth_(depth),
      // 偏移是相对本层标高的：默认偏移 = 层高 → 落在本层顶。
      elevation_(document.bim().storey_elevation(document.bim().active_storey_id()) +
                 elevation) {}

CreateBeamCommand::CreateBeamCommand(Document& document, BeamShape shape, double flange_width,
                                     double web_thickness, double height,
                                     double flange_thickness, double elevation)
    : document_(&document),
      shape_(shape),
      flange_width_(flange_width),
      web_thickness_(web_thickness),
      height_(height),
      flange_thickness_(flange_thickness),
      elevation_(document.bim().storey_elevation(document.bim().active_storey_id()) +
                 elevation) {}

CreateBeamCommand::CreateBeamCommand(Document& document, double width, double depth,
                                     Vec3 start, Vec3 end, double elevation)
    : CreateBeamCommand(document, width, depth, elevation) {
  start_ = start;
  end_ = end;
  has_start_ = true;
  scripted_ = true;
}

Result<bool> CreateBeamCommand::on_point(Vec3 point) {
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;
  }
  end_ = point;
  return true;
}

CommandArgs CreateBeamCommand::echo_args() const {
  return {{"points", std::vector<Vec3>{start_, end_}}};
}

Result<void> CreateBeamCommand::execute() {
  // 交互式：起一个 drag 采两个屏幕点。
  //
  // 为什么在这里而不是让视口 feed_point：屏幕点 → 世界点的换算、捕捉、预览
  // 都属于"采集"这件事本身，交给 drag 一起做，命令只管拿到结果建实体。
  if (!scripted_ && !has_start_) {
    DragContext* context = interaction_context();
    if (context == nullptr) {
      return Err("create_beam: no interaction context (headless dispatch?)");
    }
    // 工作面 = 本层顶：梁落在楼面下（和板的"本层顶板"同一约定）。
    // 栈对象：doIt 只借用，返回后 start()/end() 仍然可读。
    BeamDrag drag(static_cast<float>(elevation_));
    const auto end = drag.doIt(*context);
    if (!end) {
      return Err(end.error());
    }
    if (*end != DragEnd::Committed) {
      return Err(std::string(kCommandCancelled));  // 用户放弃：不是错误，交给宿主判断
    }
    // 强制落在工作面上：吸附只保证 XZ，y 以工作平面为准。
    start_ = drag.start();
    end_ = drag.end();
    start_.y = static_cast<float>(elevation_);
    end_.y = static_cast<float>(elevation_);
    has_start_ = true;
  }

  std::unique_ptr<BeamEntity> beam;
  switch (shape_) {
    case BeamShape::Tee:
      beam = std::make_unique<BeamEntity>(BeamEntity::tee(start_, end_, flange_width_,
                                                            web_thickness_, height_,
                                                            flange_thickness_));
      break;
    case BeamShape::IBeam:
      beam = std::make_unique<BeamEntity>(BeamEntity::ibeam(start_, end_, flange_width_,
                                                             web_thickness_, height_,
                                                             flange_thickness_));
      break;
    case BeamShape::Rectangular:
    default:
      beam = std::make_unique<BeamEntity>(start_, end_, width_, depth_);
      break;
  }
  auto geometry = beam->createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::move(beam), std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateBeamCommand: add_beam failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateBeamCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateBeamCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
