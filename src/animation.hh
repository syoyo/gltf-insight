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
#pragma once

#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

struct gltf_node;

#define ANIMATION_FPS 60.0f

/// Represent a loaded animation
struct animation {
  /// Represent a glTF animation channel
  struct channel {
    /// Represent the keyframe array content.
    struct keyframe_content {
      // To keep the same object, but to lower the memory footprint, we are
      // using an union here
      union {
        glm::vec3 translation;
        glm::vec3 scale;
        glm::quat rotation;
        float weight;
      } motion;

      keyframe_content();
    };

    /// Types of keyframes
    enum class path : uint8_t {
      not_assigned,
      translation,
      scale,
      rotation,
      weight
    };

    /// Storage for keyframes, they are stored with their frame index
    std::vector<std::pair<int, keyframe_content>> keyframes;

    /// Index of the sampler to be used
    int sampler_index;

    /// glTF index of the node being moved by this
    int target_node;
    /// Pointer to the mesh/skeleton graph inside gltf-insight used to display
    gltf_node* target_graph_node;
    path mode;

    /// Set everything to an "unassigned" value or equivalent
    channel();
  };

  /// Represent a gltf sampler
  struct sampler {
    enum class interpolation : uint8_t {
      not_assigned,
      step,
      linear,
      cubic_spline
    };

    /// Keyframe storage. they are in the [frame_index, time_point] format
    std::vector<std::pair<int, float>> keyframes;

    /// How to interpolate the frames between two keyframes
    interpolation mode;
    /// Min and max values
    float min_v, max_v;
    /// Set everything to "not assigned"
    sampler();
  };

  /// Data
  std::vector<channel> channels;
  std::vector<sampler> samplers;

  /// Timing
  float current_time;
  float min_time, max_time;

  /// Set to true when playing
  bool playing;

  /// Name of the animation
  std::string name;

  /// Set the current play state of the animation
  void set_playing_state(bool state = true);

  /// Add time forward
  void add_time(float delta);

  /// Set the animation to the specified timepoint
  void set_time(float time);

  /// Checks the minimal and maximal times in samplers
  void compute_time_boundaries();

  /// Construct an empty animation object
  animation();

  /// Assign to each animation channel a pointer to the node they control
  void set_gltf_graph_targets(gltf_node* root_node);

  /// Apply the pose at "current time" to the animated objects
  void apply_pose();

 private:
  /// Apply the pose in the animation channel for the given interpolation mode
  void apply_channel_target_for_interpolation_value(
      float interpolation_value, sampler::interpolation mode, int lower_frame,
      int upper_frame, float lower_time, float upper_time, const channel& chan);

  /// just apply the lower keyframe state
  void apply_step(const channel& chan, int lower_keyframe);

  /// just glm::mix all of the components for vectors, and slerp for
  /// quaternions?
  void apply_linear(const channel& chan, int lower_keyframe, int upper_keyframe,
                    float mix);

  /// Compute the cubic spline interpolation
  /// See
  /// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#appendix-c-spline-interpolation
  template <typename T>
  inline static T cubic_spline_interpolate(float t, T p0, T m0, T p1, T m1) {
    const auto t2 = t * t;
    const auto t3 = t2 * t;

    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 +
           (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
  }

  /// Apply cubic spline sampling
  void apply_cubic_spline(float interpolation_value, int lower_frame,
                          int upper_frame, float lower_time, float upper_time,
                          const channel& chan);
};
