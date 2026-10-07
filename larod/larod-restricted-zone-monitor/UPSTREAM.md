# Upstream source and local adaptations

Base: Axis Communications, `acap-native-sdk-examples/object-detection`, commit
[`3025a4e9db74f1af5bb878d4efc82ac0e300a792`](https://github.com/AxisCommunications/acap-native-sdk-examples/tree/3025a4e9db74f1af5bb878d4efc82ac0e300a792/object-detection).

The imported model, preprocessing, image, channel, argument, and panic helpers
retain their Axis copyright and Apache-2.0 headers. The license is in
[app/LICENSE](app/LICENSE). `stream.c` extracts stream setup from the upstream
`object_detection.c`; the workshop has its own `main.c` and industrial policy.

Local changes to the imported inference helpers:

- Correct the output index bound check (`>= num_outputs`).
- Keep the tracked-input count in the provider rather than a function static.
- Initialize tracked fds to -1 and check camera-buffer pool limits.
- Close the temporary converted vmem fd after duplicating it.
- Destroy jobs, mappings, tensors, and model handles before disconnecting larod.
- Keep inference power retries short so application status can update between attempts.
- Require exactly one inference input.
- Apply the workshop's readable formatting.

The new SSD decoder checks float types and mapped-buffer capacities and retains
only person detections. The policy, events, FastCGI status, and dashboard are
workshop additions. The status-worker and video-preview patterns come from the
existing workshop applications.

The build downloads the same Coral SSD MobileNet v2 model family and COCO label
file used by upstream from [google-coral/test_data](https://github.com/google-coral/test_data).
The Dockerfile identifies the model variant for each backend. The labels are
included for students to inspect; runtime filtering uses the bundled model's
zero-based person class.

The optional model-input preview adds a bounded RGB tensor copy immediately before
`larodRunJob()` for inference. Normal inference keeps this disabled. It does not
replace preprocessing or alter input pixels. See `input_preview.c` and the README.

Live view switching adds `model_provider_reset_stream()`. It releases per-stream
jobs, VDO input tensors, duplicated descriptors, and preprocessing resources while
retaining the inference model and mapped outputs. Preprocessing cleanup calls
`larodDeleteModel()` before `larodDestroyModel()`: the latter only releases the
local handle, as documented in the
[larod API](https://developer.axis.com/acap/acap-native-sdk-version-12/api/src/api/larod/html/larod_8h.html).
Stream creation and resolution discovery report recoverable failures to the main
loop, which keeps settings reachable and retries the requested view.
