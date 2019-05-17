#include "gltf-graph.hh"

gltf_node::~gltf_node() = default;

gltf_node::gltf_node(node_type t, gltf_node* p)
    : type(t),
      local_xform(1.f),
      world_xform(1.f),
      gltf_model_node_index(-1),
      parent(p) {}

void gltf_node::add_child_bone(glm::mat4 local_xform) {
  gltf_node* child = new gltf_node(node_type::bone);
  child->local_xform = local_xform;
  child->parent = this;
  children.emplace_back(child);
}

gltf_node* gltf_node::get_ptr() { return this; }

gltf_node* find_index_in_children(gltf_node* node, int index) {
  if (node && node->gltf_model_node_index == index) return node;

  for (auto child : node->children) {
    if (child->gltf_model_node_index == index) return child->get_ptr();
    return find_index_in_children(child->get_ptr(), index);
  }

  return nullptr;
}

gltf_node* gltf_node::get_node_with_index(int index) {
  auto* node = find_index_in_children(this, index);
  if (node) {
    assert(node->gltf_model_node_index == index);
  }
  return node;
}