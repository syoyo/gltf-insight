#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "gltf-graph.hh"

#include "gl_util.hh"
#include "tiny_gltf_util.h"

bool draw_joint_point = true;
bool draw_bone_segment = true;
bool draw_childless_bone_extension = true;
bool draw_mesh_anchor_point = false;
bool draw_bone_axes = false;

gltf_node::~gltf_node() = default;

gltf_node::gltf_node(node_type t, gltf_node* p)
    : type(t), local_xform(1.f), world_xform(1.f), parent(p) {}

void gltf_node::add_child() {
  gltf_node* child = new gltf_node(node_type::empty);
  child->parent = this;
  children.emplace_back(child);
}

gltf_node* gltf_node::get_ptr() { return this; }

gltf_node* find_index_in_children(gltf_node* node, int index) {
  if (node->gltf_node_index == index) return node;

  gltf_node* found = nullptr;
  for (auto child : node->children) {
    found = find_index_in_children(child->get_ptr(), index);
    if (found) return found;
  }

  return nullptr;
}

gltf_node* gltf_node::get_node_with_index(int index) {
  auto* node = find_index_in_children(this, index);

  if (node) {
    assert(node->gltf_node_index == index);
  }
  return node;
}

void update_mesh_skeleton_graph_transforms(gltf_node& node,
                                           glm::mat4 parent_matrix) {
  // Calculate a matrix that apply the "animated pose" transform to the node
  glm::mat4 pose_translation =
      glm::translate(glm::mat4(1.f), node.pose.translation);
  glm::mat4 pose_rotation = glm::toMat4(node.pose.rotation);
  glm::mat4 pose_scale = glm::scale(glm::mat4(1.f), node.pose.scale);
  glm::mat4 pose_matrix = pose_translation * pose_rotation * pose_scale;

  // The calculated "pose_matrix" is actually the absolute position on the local
  // space. I prefer to keep the "binding pose" of the skeleton, and reference
  // key frames as a "delta" from these ones. If that pose_matrix is "identity"
  // it means that the node hasn't been moved.

  // Set the transform as a "delta" from the local transform
  if (glm::mat4(1.f) == pose_matrix) {
    pose_matrix = node.local_xform;
  }

  /* This will accumulate the parent/child matrices to get everything in the
   * referential of `node`
   *
   * The node's "local" transform is the natural (binding) pose of the node
   * relative to it's parent. The calculated "pose_matrix" is how much the
   * node needs to be moved in space, relative to it's parent from this
   * binding pose to be in the correct transform the animation wants it to
   * be. The parent_matrix is the "world_transform" of the parent node,
   * recusively passed down along the graph.
   *
   * The content of the node's "pose" structure used here will be updated by
   * the animation playing system in accordance to it's current clock,
   * interpolating between key frames (see class defined in animation.hh)
   */

  node.world_xform = parent_matrix * node.local_xform *
                     (node.type != gltf_node::node_type::mesh
                          ? glm::inverse(node.local_xform) * pose_matrix
                          : glm::mat4(1.f));

  // recursively call itself until you reach a node with no children
  for (auto& child : node.children)
    update_mesh_skeleton_graph_transforms(*child, node.world_xform);
}

// skeleton_index is the first node that will be added as a child of
// "graph_root" e.g: a gltf node has a mesh. That mesh has a skin, and that
// skin as a node index as "skeleton". You need to pass that "skeleton"
// integer to this function as skeleton_index. This returns a flat array of
// the bones to be used by the skinning code
void populate_gltf_graph(const tinygltf::Model& model, gltf_node& graph_root,
                         int gltf_index)

{
  // get the gltf node object
  const auto& root_node = model.nodes[gltf_index];

  // Holder for the data.
  glm::mat4 xform(1.f);
  glm::vec3 translation(0.f), scale(1.f, 1.f, 1.f);
  glm::quat rotation(1.f, 0.f, 0.f, 0.f);

  // A node can store both a transform matrix and separate translation, rotation
  // and scale. We need to load them if they are present. tiny_gltf signal this
  // by either having empty vectors, or vectors of the expected size.
  const auto& node_matrix = root_node.matrix;
  if (node_matrix.size() == 16)  // 4x4 matrix
  {
    double tmp[16];
    float tmpf[16];
    memcpy(tmp, root_node.matrix.data(), 16 * sizeof(double));

    // Convert a double array to a float array
    for (int i = 0; i < 16; ++i) {
      tmpf[i] = float(tmp[i]);
    }

    // Both glm matrices and this float array have the same data layout. We can
    // pass the pointer to glm::make_mat4
    xform = glm::make_mat4(tmpf);
  }

  // Do the same for translation rotation and scale.
  const auto& node_translation = root_node.translation;
  if (node_translation.size() == 3)  // 3D vector
  {
    for (glm::vec3::length_type i = 0; i < 3; ++i)
      translation[i] = float(node_translation[i]);
  }

  const auto& node_scale = root_node.scale;
  if (node_scale.size() == 3)  // 3D vector
  {
    for (glm::vec3::length_type i = 0; i < 3; ++i)
      scale[i] = float(node_scale[i]);
  }

  const auto& node_rotation = root_node.rotation;
  if (node_rotation.size() == 4)  // Quaternion
  {
    rotation.w = float(node_rotation[3]);
    rotation.x = float(node_rotation[0]);
    rotation.y = float(node_rotation[1]);
    rotation.z = float(node_rotation[2]);
    glm::normalize(rotation);  // Be prudent
  }

  const glm::mat4 rotation_matrix = glm::toMat4(rotation);
  const glm::mat4 translation_matrix =
      glm::translate(glm::mat4(1.f), translation);
  const glm::mat4 scale_matrix = glm::scale(glm::mat4(1.f), scale);
  const glm::mat4 reconstructed_matrix =
      translation_matrix * rotation_matrix * scale_matrix;

  // In the case that the node has both the matrix and the individual vectors,
  // both transform has to be applied.
  xform = xform * reconstructed_matrix;

  // set data inside root
  graph_root.local_xform = xform;
  graph_root.gltf_node_index = gltf_index;

  for (int child : root_node.children) {
    // push a new children
    graph_root.add_child();
    // get the children object
    auto& new_node = *graph_root.children.back().get();
    // recurse
    populate_gltf_graph(model, new_node, child);
  }
}

void set_mesh_attachement(const tinygltf::Model& model, gltf_node& graph_root) {
  if (graph_root.gltf_node_index != -1) {
    const auto& node = model.nodes[graph_root.gltf_node_index];

    if (has_mesh(node)) {
      graph_root.gltf_mesh_id = node.mesh;
      graph_root.type = gltf_node::node_type::mesh;
    }
  }
  for (auto& child : graph_root.children) set_mesh_attachement(model, *child);
}

void get_number_of_meshes_recur(const gltf_node& root, size_t& number) {
  if (root.type == gltf_node::node_type::mesh) ++number;
  for (auto child : root.children) {
    get_number_of_meshes_recur(*child, number);
  }
}

void get_list_of_mesh_recur(const gltf_node& root,
                            std::vector<gltf_mesh_instance>& meshes) {
  if (root.type == gltf_node::node_type::mesh) {
    gltf_mesh_instance inst;
    inst.mesh = root.gltf_mesh_id;
    inst.node = root.gltf_node_index;
    meshes.push_back(inst);
  }

  for (auto child : root.children) get_list_of_mesh_recur(*child, meshes);
}

std::vector<gltf_mesh_instance> get_list_of_mesh_instances(
    const gltf_node& root) {
  std::vector<gltf_mesh_instance> meshes;
  get_list_of_mesh_recur(root, meshes);
  return meshes;
}

void draw_line(GLuint shader, const glm::vec3 origin, const glm::vec3 end,
               const glm::vec4 draw_color, const float line_width) {
  glUseProgram(shader);
  glLineWidth(line_width);
  glUniform4f(glGetUniformLocation(shader, "debug_color"), draw_color.r,
              draw_color.g, draw_color.b, draw_color.a);
  glBegin(GL_LINES);
  glVertex4f(end.x, end.y, end.z, 1);
  glVertex4f(origin.x, origin.y, origin.z, 1);
  glEnd();
}

void draw_bones(gltf_node& root, GLuint shader, glm::mat4 view_matrix,
                glm::mat4 projection_matrix) {
  // recurse down the scene tree
  for (auto& child : root.children)
    draw_bones(*child, shader, view_matrix, projection_matrix);

  // on current node, set debug_shader model view projection to draw our local
  // space
  glUseProgram(shader);
  glm::mat4 mvp = projection_matrix * view_matrix * root.world_xform;
  glUniformMatrix4fv(glGetUniformLocation(shader, "mvp"), 1, GL_FALSE,
                     glm::value_ptr(mvp));

  // draw XYZ axes
  if (draw_bone_axes) draw_space_base(shader, 2.f, .125f);

  // If this node is actually a bone
  if (root.type == gltf_node::node_type::bone) {
    // Draw segment to child bones
    if (draw_bone_segment) {
      for (auto child : root.children) {
        if (child->type == gltf_node::node_type::bone) {
          draw_line(shader, glm::vec3(0.f), child->local_xform[3],
                    glm::vec4(0.f, .5f, .5f, 1.f), 8);
        }
      }

      // The "last" bone of many rig doesn't have a child joint. Draw a line in
      // another color to show it anyway TODO make lenght a setting
      if (draw_childless_bone_extension && root.children.empty())
        draw_line(shader, glm::vec3(0.f),
                  glm::vec3(0.f, .25f, 0.f) /*Y is length*/,
                  glm::vec4(.5f, .75f, .5f, 1.f), 6);
    }

    // Red dot on joint
    if (draw_joint_point)
      draw_space_origin_point(10, shader, glm::vec4(1, 0, 0, 1));
  }

  // Yellow dot on mesh pivot
  if (draw_mesh_anchor_point && root.type == gltf_node::node_type::mesh)
    draw_space_origin_point(10, shader, glm::vec4(1, 1, 0, 1));

  // Unset shader program
  glUseProgram(0);
}

void create_flat_bone_array(gltf_node& root,
                            std::vector<gltf_node*>& flat_array,
                            const std::vector<int>& skin_joints) {
  const auto find_result =
      std::find(skin_joints.cbegin(), skin_joints.cend(), root.gltf_node_index);
  if (find_result != skin_joints.cend()) {
    root.type = gltf_node::node_type::bone;
    flat_array.push_back(root.get_ptr());
  }

  for (auto& child : root.children)
    create_flat_bone_array(*child, flat_array, skin_joints);
}

void sort_bone_array(std::vector<gltf_node*>& bone_array,
                     const tinygltf::Skin& skin_object) {
  // assert(bone_array.size() == skin_object.joints.size());
  for (size_t counter = 0;
       counter < std::min(bone_array.size(), skin_object.joints.size());
       ++counter) {
    const int index_to_find = skin_object.joints[counter];
    for (size_t bone_index = 0; bone_index < bone_array.size(); ++bone_index) {
      if (bone_array[bone_index]->gltf_node_index == index_to_find) {
        std::swap(bone_array[counter], bone_array[bone_index]);
        break;
      }
    }
  }
}

void create_flat_bone_list(const tinygltf::Skin& skin,
                           const std::vector<int>::size_type nb_joints,
                           gltf_node mesh_skeleton_graph,
                           std::vector<gltf_node*>& flatened_bone_list) {
  create_flat_bone_array(mesh_skeleton_graph, flatened_bone_list, skin.joints);
  sort_bone_array(flatened_bone_list, skin);
}

// This is useful because mesh.skeleton isn't required to point to the
// skeleton root. We still want to find the skeleton root, so we are going to
// search for it by hand.
int find_skeleton_root(const tinygltf::Model& model,
                       const std::vector<int>& joints, int start_node) {
  // Get the node to get the children
  const auto& node = model.nodes[start_node];

  for (int child : node.children) {
    // If we are part of the skeleton, return our parent
    for (int joint : joints) {
      if (joint == child) return joint;
    }
    // try to find in children, if found return the child's child's parent
    // (so, here, the retunred value by find_skeleton_root)
    const int result = find_skeleton_root(model, joints, child);
    if (result != -1) return result;
  }

  // Here it means we found nothing, return "nothing"
  return -1;
}

#include <imgui.h>
void bone_display_window() {
  if (ImGui::Begin("Skeleton drawing options")) {
    ImGui::Checkbox("Draw joint points", &draw_joint_point);
    ImGui::Checkbox("Draw Bone as segments", &draw_bone_segment);
    ImGui::Checkbox("Draw childless joint extnesion",
                    &draw_childless_bone_extension);
    ImGui::Checkbox("Draw Bone's base axes", &draw_bone_axes);
    ImGui::Checkbox("Draw Skeleton's Mesh base", &draw_mesh_anchor_point);
  }
  ImGui::End();
}
