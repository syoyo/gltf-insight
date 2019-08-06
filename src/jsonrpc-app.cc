/*
MIT License

Copyright (c) 2019 Light Transport Entertainment Inc. And many contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "insight-app.hh"
#include "jsonrpc-http.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// Include tinygltf's json.hpp
#include "json.hpp"

#include "fmt/core.h"

#include "glm/gtc/quaternion.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "jsonrpc-command.hh"

#include <iostream>
#include <vector>
#include <thread>

using nlohmann::json;

namespace gltf_insight {

bool app::spawn_http_listen()
{
#if !defined(GLTF_INSIGHT_WITH_JSONRPC)
  return false;
#else
  std::cout << "Start thread\n";

  _jsonrpc_exit_flag = false;

  std::function<void(const std::string&)> cb_f = [&](const std::string &msg) {
    bool ret =  this->jsonrpc_dispatch(msg);
    // TODO(LTE): Check return value.
    (void)ret;

  };

  JSONRPC rpc;
  std::cout << "Listen...\n";
  bool ret = rpc.listen_blocking(cb_f, &_jsonrpc_exit_flag, _address, _port);
  std::cout << "Listen ret = " << ret << "\n";

  if (!ret) {
    _http_failed = true;
    return false;
  }

  return true;
#endif

}

// req_len = -1 => allow arbitrary array length
static bool DecodeNumberArray(const json &j, int req_len, std::vector<float> *values) {

  if (values == nullptr) {
    return false;
  }

  values->clear();

  if (!j.is_array()) {
    return false;
  }

  if (req_len > 0) {
    if (req_len != int(j.size())) {
      fmt::print("Array length must be {} but got {}.\n", req_len, j.size());
        return false;
    }
  }

  for (auto& elem : j) {
    if (!elem.is_number()) {
      std::cerr << "non-number value in an array found.\n";
      return false;
    }

    float value = float(elem.get<double>());

    values->push_back(value);
  }

  return true;
}

static bool DecodeMorphWeights(const json &j, std::vector<std::pair<int, float>> *params)
{
  params->clear();

  if (!j.is_array()) {
    std::cerr << "morph_weights must be an array object.\n";
    return false;
  }

  for (auto& elem : j) {
    if (!elem.count("target_id")) {
      std::cerr << "`target_id` is missing in morph_weight object.\n";
      continue;
    }

    json j_target_id = elem["target_id"];
    if (!j_target_id.is_number()) {
      std::cerr << "`target_id` must be a number value.\n";
      continue;
    }

    int target_id = j_target_id.get<int>();
    std::cout << "target_id " << target_id << "\n";

    if (!elem.count("weight")) {
      std::cerr << "`weight` is missing in morph_weight object.\n";
      continue;
    }

    json j_weight = elem["weight"];
    if (!j_weight.is_number()) {
      std::cerr << "`weight` must be a number value.\n";
      continue;
    }

    float weight = float(j_weight.get<double>());
    std::cout << "weight " << weight << "\n";

    params->push_back({target_id, weight});

  }

  return true;
}

static bool DecodeNodeTransforms(const json &j, std::vector<std::pair<int, Xform>> *params)
{
  params->clear();

  if (!j.is_array()) {
    std::cerr << "node_transforms must be an array object.\n";
    return false;
  }

  for (auto& elem : j) {
    if (!elem.count("joint_id")) {
      std::cerr << "`joint_id` is missing in node_transform object.\n";
      continue;
    }

    json j_joint_id = elem["joint_id"];
    if (!j_joint_id.is_number()) {
      std::cerr << "`joint_id` must be a number value.\n";
      continue;
    }

    int joint_id = j_joint_id.get<int>();
    std::cout << "joint_id " << joint_id << "\n";

    Xform xform;

    if (elem.count("translation")) {
      json j_translation = elem["translation"];

      std::vector<float> translation;
      if (!DecodeNumberArray(j_translation, /* length */3, &translation)) {
        std::cerr << "Failed to decode `translation` parameter.\n";
        continue;
      }

      xform.translation[0] = translation[0];
      xform.translation[1] = translation[1];
      xform.translation[2] = translation[2];


    } else if (elem.count("scale")) {
      json j_scale = elem["scale"];

      std::vector<float> scale;
      if (!DecodeNumberArray(j_scale, /* length */3, &scale)) {
        std::cerr << "Failed to decode `scale` parameter.\n";
        continue;
      }

      xform.scale[0] = scale[0];
      xform.scale[1] = scale[1];
      xform.scale[2] = scale[2];

    } else if (elem.count("rotate")) { // quaternion
      json j_rotate = elem["rotate"];

      std::vector<float> rotate;
      if (!DecodeNumberArray(j_rotate, /* length */4, &rotate)) {
        std::cerr << "Failed to decode `rotate` parameter.\n";
        continue;
      }

      xform.rotate[0] = rotate[0];
      xform.rotate[1] = rotate[1];
      xform.rotate[2] = rotate[2];
      xform.rotate[3] = rotate[3];

    } else if (elem.count("rotate_angle")) { // euler XYZ
      json j_rotate = elem["rotate_angle"];

      std::vector<float> angle;
      if (!DecodeNumberArray(j_rotate, /* length */3, &angle)) {
        std::cerr << "Failed to decode `rotate_angle` parameter.\n";
        continue;
      }

      // to quaternion. value is in radian!
      glm::quat q = glm::quat(glm::vec3(glm::radians(angle[0]), glm::radians(angle[1]), glm::radians(angle[2])));

      xform.rotate[0] = q.x;
      xform.rotate[1] = q.y;
      xform.rotate[2] = q.z;
      xform.rotate[3] = q.w;
    }

    params->push_back({joint_id, xform});

  }

  return true;
}


bool app::jsonrpc_dispatch(const std::string &json_str)
{
  json j;

  try {
    j = json::parse(json_str);
  } catch (const std::exception &e) {
    fmt::print("Invalid JSON string. what = {}\n", e.what());
    return false;
  }

  if (!j.is_object()) {
    std::cerr << "Invalid JSON message.\n";
    return false;
  }

  if (!j.count("jsonrpc")) {
    std::cerr << "JSON message does not contain `jsonrpc`.\n";
    return false;
  }

  if (!j["jsonrpc"].is_string()) {
    std::cerr << "value for `jsonrpc` must be string.\n";
    return false;
  }

  std::string version = j["jsonrpc"].get<std::string>();
  if (version.compare("2.0") != 0) {
    std::cerr << "JSONRPC version must be \"2.0\" but got \"" << version << "\"\n";
    return false;
  }

  if (!j.count("method")) {
    std::cerr << "JSON message does not contain `method`.\n";
    return false;
  }

  if (!j["method"].is_string()) {
    std::cerr << "`method` must be string.\n";
    return false;
  }

  std::string method = j["method"].get<std::string>();
  if (method.compare("update") != 0) {
    std::cerr << "`method` must be `update`, but got `" << method << "'\n";
    return false;
  }

  if (!j.count("params")) {
    std::cerr << "JSON message does not contain `params`.\n";
    return false;
  }

  json params = j["params"];

  if (params.count("morph_weights")) {
    json morph_weights = params["morph_weights"];

    std::vector<std::pair<int, float>> morph_params;
    bool ret = DecodeMorphWeights(morph_weights, &morph_params);
    if (ret) {
      std::cout << "Update morph_weights " << morph_weights << "\n";
    }

    // Add update request to command queue, since directly update
    // gltf_scene_tree from non-main thread will cause segmentation fault.
    // Updating the scene must be done in main thead.
    {
      std::lock_guard<std::mutex> lock(_command_queue_mutex);

      for (size_t i = 0; i < morph_params.size(); i++) {
        Command command;
        command.type = Command::MORPH_WEIGHT;
        command.morph_weight = morph_params[i];

        _command_queue.push(command);
      }
    }

  } else if (params.count("joint_transforms")) {
    json joint_transforms = params["joint_transforms"];

    std::vector<std::pair<int, Xform>> transform_params;
    bool ret = DecodeNodeTransforms(joint_transforms, &transform_params);
    if (ret) {
      std::cout << "Update joint_transforms " << joint_transforms << "\n";
    }

    // Add update request to command queue, since directly update
    // gltf_scene_tree from non-main thread will cause segmentation fault.
    // Updating the scene must be done in main thead.
    {
      std::lock_guard<std::mutex> lock(_command_queue_mutex);

      for (size_t i = 0; i < joint_transforms.size(); i++) {
        Command command;
        command.type = Command::JOINT_TRANSFORM;
        command.joint_transform = transform_params[i];

        _command_queue.push(command);
      }
    }

  }

  return true;
}

bool app::update_scene(const Command &command)
{

  if (command.type == Command::MORPH_WEIGHT) {
    std::pair<int, float> param = command.morph_weight;

    if (gltf_scene_tree.pose.blend_weights.size() > 0) {
      int idx = param.first;

      std::cout << idx << ", # of morphs = " << gltf_scene_tree.pose.blend_weights.size() << "\n";

      if ((idx >= 0) && (idx < int(gltf_scene_tree.pose.blend_weights.size()))) {
        float weight = std::min(1.0f, std::max(0.0f, param.second));

        std::cout << "Update " << idx << "th morph_weight with " << weight << "\n";
        gltf_scene_tree.pose.blend_weights[size_t(idx)] = weight;
      }
    }

  } else if (command.type == Command::JOINT_TRANSFORM) {
    std::pair<int, Xform> param = command.joint_transform;

    int joint_id = param.first;
    const Xform &xform = param.second;

    if ((joint_id >= 0) && (joint_id < int(loaded_meshes[0].flat_joint_list.size()))) {
      gltf_node* joint = loaded_meshes[0].flat_joint_list[size_t(joint_id)];

      joint->pose.translation.x = xform.translation[0];
      joint->pose.translation.y = xform.translation[1];
      joint->pose.translation.z = xform.translation[2];

      joint->pose.rotation.x = xform.rotate[0];
      joint->pose.rotation.y = xform.rotate[1];
      joint->pose.rotation.z = xform.rotate[2];
      joint->pose.rotation.w = xform.rotate[3];

      joint->pose.scale.x = xform.scale[0];
      joint->pose.scale.y = xform.scale[1];
      joint->pose.scale.z = xform.scale[2];
    } else {
      fmt::print("Invalid joint ID {}. joints.size = {}\n", joint_id, loaded_meshes[0].flat_joint_list.size());
    }

  }

  return true;
}

} // namespace gltf_insight