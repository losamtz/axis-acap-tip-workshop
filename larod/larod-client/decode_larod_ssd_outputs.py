import os
import glob
import numpy as np

# Change this if your files have different names
PATTERN = "test_out*.bin"

# Image size you used when creating test.bin in get_binary.py
IMG_W = 300
IMG_H = 300

def main():
    files = sorted(glob.glob(PATTERN))
    if len(files) != 4:
        raise RuntimeError(f"Expected 4 output files, found {len(files)}: {files}")

    outputs = []
    for f in files:
        size_bytes = os.path.getsize(f)
        n_floats = size_bytes // 4  # float32 -> 4 bytes
        data = np.fromfile(f, dtype=np.float32)
        print(f"{f}: {size_bytes} bytes, {n_floats} float32 values")
        outputs.append({
            "file": f,
            "size_bytes": size_bytes,
            "n_floats": n_floats,
            "data": data,
        })

    # Sort by number of floats: smallest -> num_detections, largest -> boxes
    outputs_sorted = sorted(outputs, key=lambda x: x["n_floats"])

    num_det_out = outputs_sorted[0]      # 1 float
    mid1_out    = outputs_sorted[1]      # N floats
    mid2_out    = outputs_sorted[2]      # N floats
    boxes_out   = outputs_sorted[3]      # N*4 floats

    # Infer number of boxes from the "boxes" output
    if boxes_out["n_floats"] % 4 != 0:
        raise RuntimeError("Boxes output size is not divisible by 4, unexpected format.")

    num_boxes = boxes_out["n_floats"] // 4
    print(f"Inferred num_boxes = {num_boxes}")

    # Reshape tensors
    boxes   = boxes_out["data"].reshape(1, num_boxes, 4)
    arr1    = mid1_out["data"].reshape(1, num_boxes)
    arr2    = mid2_out["data"].reshape(1, num_boxes)
    num_det = int(num_det_out["data"][0])

    # Heuristic: decide which middle one is scores vs classes
    # Scores are usually between 0 and 1, classes are typically integers (0..90 etc)
    mean1, mean2 = arr1.mean(), arr2.mean()
    print(f"mid1 mean={mean1:.4f}, mid2 mean={mean2:.4f}")

    if 0.0 <= mean1 <= 1.0 and mean2 > 1.0:
        scores = arr1
        classes = arr2
        print(f"{mid1_out['file']} interpreted as scores, {mid2_out['file']} as classes")
    elif 0.0 <= mean2 <= 1.0 and mean1 > 1.0:
        scores = arr2
        classes = arr1
        print(f"{mid2_out['file']} interpreted as scores, {mid1_out['file']} as classes")
    else:
        # Fallback: assume output order: 0=boxes, 1=classes, 2=scores, 3=numDet
        print("Could not distinguish scores/classes by mean; falling back to fixed assignment.")
        # This fallback assumes your '-o' mapping matches model's output index order.
        scores = arr2
        classes = arr1

    # Clamp num_detections in case it's larger than num_boxes
    n = min(num_det, num_boxes)
    print(f"Model reports num_detections={num_det}, using n={n}")

    # Print detected boxes with score >= 0.3
    SCORE_THRESH = 0.3
    for i in range(n):
        score = float(scores[0, i])
        if score < SCORE_THRESH:
            continue

        cls = int(classes[0, i])
        y_min, x_min, y_max, x_max = boxes[0, i]

        # Convert normalized coords -> pixels in resized 300x300 image
        x_min_px = int(x_min * IMG_W)
        x_max_px = int(x_max * IMG_W)
        y_min_px = int(y_min * IMG_H)
        y_max_px = int(y_max * IMG_H)

        print(
            f"Det {i}: class={cls}, score={score:.2f}, "
            f"box_norm=(ymin={y_min:.2f}, xmin={x_min:.2f}, ymax={y_max:.2f}, xmax={x_max:.2f}), "
            f"box_px=(x_min={x_min_px}, y_min={y_min_px}, x_max={x_max_px}, y_max={y_max_px})"
        )

if __name__ == "__main__":
    main()

