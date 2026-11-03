"""Offline GPU vehicle detection + persistent IDs. Does not access PYNQ/UART.

Baseline detector: official Ultralytics YOLOv5su (not TomMao PSP weights).
ByteTrack keeps lost associations for a bounded time, without drawing ghosts.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent
os.environ.setdefault('YOLO_CONFIG_DIR', str(ROOT / '.cache/ultralytics'))
os.environ.setdefault('MPLCONFIGDIR', str(ROOT / '.cache/matplotlib'))
os.environ.setdefault('CUDA_CACHE_PATH', str(ROOT / '.cache/cuda'))
os.environ['YOLO_AUTOINSTALL'] = 'false'
Path(os.environ['YOLO_CONFIG_DIR']).mkdir(parents=True, exist_ok=True)

import cv2
import numpy as np
import torch
import ultralytics
from ultralytics import YOLO
from ultralytics.trackers.byte_tracker import BYTETracker


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def tracker_arguments(lost_seconds: float) -> SimpleNamespace:
    # BYTETracker internally scales track_buffer by video frame rate / 30.
    return SimpleNamespace(track_high_thresh=0.35, track_low_thresh=0.10,
                           new_track_thresh=0.40, match_thresh=0.80,
                           track_buffer=max(1, round(lost_seconds * 30)),
                           fuse_score=True)


def observed_tracks(tracker, boxes, image, names) -> list[dict]:
    rows = tracker.update(boxes.cpu().numpy(), image)
    h, w = image.shape[:2]
    objects = []
    for row in rows:
        x1, y1, x2, y2 = map(float, row[:4])
        x1, y1 = max(0.0, x1), max(0.0, y1)
        x2, y2 = min(w - 1.0, x2), min(h - 1.0, y2)
        if x2 <= x1 or y2 <= y1:
            continue
        cls = int(row[6])
        objects.append({'id': int(row[4]), 'class_id': cls, 'class_name': names[cls],
                        'confidence': round(float(row[5]), 5),
                        'bbox_xyxy': [round(q, 2) for q in (x1, y1, x2, y2)],
                        'observed': True})
    assert len({o['id'] for o in objects}) == len(objects)
    return objects


def annotate(frame, objects, timestamp, device_name):
    image = frame.copy()
    font_scale = max(0.55, image.shape[1] * 0.85 / 1920)
    thickness = max(2, round(image.shape[1] / 640))
    for obj in objects:
        x1, y1, x2, y2 = map(round, obj['bbox_xyxy'])
        text = f"ID {obj['id']}  {obj['class_name']}"
        (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, 1)
        label_y = max(th + 5, y1)
        cv2.rectangle(image, (x1, y1), (x2, y2), (0, 255, 70), thickness)
        cv2.rectangle(image, (x1, label_y - th - 5),
                      (min(image.shape[1] - 1, x1 + tw + 8), label_y + 2), (0, 110, 25), -1)
        cv2.putText(image, text, (x1 + 3, label_y - 2), cv2.FONT_HERSHEY_SIMPLEX,
                    font_scale, (255, 255, 255), 1, cv2.LINE_AA)
    text = f'YOLOv5su + ByteTrack | {timestamp:.2f}s | visible {len(objects)} | {device_name}'
    cv2.rectangle(image, (0, 0), (image.shape[1] - 1, 25), (18, 18, 18), -1)
    cv2.putText(image, text, (7, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.48,
                (255, 255, 255), 1, cv2.LINE_AA)
    return image


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Original unannotated local video')
    parser.add_argument('--weights', type=Path, default=ROOT / 'models/yolov5su.pt')
    parser.add_argument('--device', default='0', help='CUDA GPU index, or explicitly cpu')
    parser.add_argument('--imgsz', type=int, default=960)
    parser.add_argument('--classes', type=int, nargs='+', default=[2, 3, 5, 7],
                        help='COCO: 2 car, 3 motorcycle, 5 bus, 7 truck')
    parser.add_argument('--lost-seconds', type=float, default=2.0)
    parser.add_argument('--ffmpeg', type=Path,
                        default=Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--start', type=float, default=0.0)
    parser.add_argument('--duration', type=float, default=0.0, help='0 = through end of file')
    return parser.parse_args()


def main():
    args = parse_args()
    for p in (args.source, args.weights, args.ffmpeg):
        if not p.is_file():
            raise FileNotFoundError(p)
    if args.device != 'cpu' and not torch.cuda.is_available():
        raise RuntimeError('CUDA unavailable; no silent CPU fallback. Check the project-local environment.')
    if not 0 < args.lost_seconds <= 10 or args.start < 0 or args.duration < 0:
        raise ValueError('Invalid tracking lifetime/video interval')
    run = args.output or ROOT / 'runs' / (args.source.stem + '_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
    run.mkdir(parents=True, exist_ok=False)
    cap = cv2.VideoCapture(str(args.source))
    if not cap.isOpened():
        raise RuntimeError('Cannot open original video')
    fps = cap.get(cv2.CAP_PROP_FPS)
    if not np.isfinite(fps) or not 1 <= fps <= 240:
        raise ValueError(f'Invalid source frame rate: {fps}')
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    start_frame = round(args.start * fps)
    if start_frame >= total_frames:
        raise ValueError('Requested start is after end of video')
    cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)
    ok, first = cap.read()
    if not ok:
        raise RuntimeError('Cannot read starting frame')
    h, w = first.shape[:2]
    model = YOLO(str(args.weights))
    gpu_name = torch.cuda.get_device_name(int(args.device)) if args.device != 'cpu' else 'CPU'
    options = dict(device=args.device, imgsz=args.imgsz, conf=0.10, iou=0.60,
                   classes=args.classes, half=args.device != 'cpu', verbose=False,
                   max_det=100)
    # Real GPU warm-up, excluded from the steady-state timing distribution.
    for _ in range(3):
        model.predict(np.zeros_like(first), **options)
    tracker = BYTETracker(tracker_arguments(args.lost_seconds), frame_rate=fps)
    output = run / 'tracked.mp4'
    ff = subprocess.Popen([str(args.ffmpeg), '-v', 'error', '-n', '-f', 'rawvideo',
                           '-pixel_format', 'bgr24', '-video_size', f'{w}x{h}',
                           '-framerate', str(fps), '-i', '-', '-an', '-vf',
                           'pad=ceil(iw/2)*2:ceil(ih/2)*2', '-c:v', 'libx264',
                           '-preset', 'fast', '-crf', '20', '-pix_fmt', 'yuv420p',
                           '-movflags', '+faststart', str(output)], stdin=subprocess.PIPE,
                          stderr=(run / 'encode.log').open('wb'))
    counts = Counter()
    lifetimes = {}
    timing_ms = []
    processed = 0
    frame = first
    wall_start = time.perf_counter()
    try:
        with (run / 'tracks.jsonl').open('w', encoding='utf-8') as tracks:
            while True:
                frame_index = start_frame + processed
                if args.duration and processed / fps >= args.duration:
                    break
                started = time.perf_counter()
                prediction = model.predict(frame, **options)[0]
                objects = observed_tracks(tracker, prediction.boxes, frame, model.names)
                elapsed = (time.perf_counter() - started) * 1000
                timing_ms.append(elapsed)
                row = {'source': 'PC_GPU_VIDEO_FILE' if args.device != 'cpu' else 'PC_CPU_VIDEO_FILE',
                       'frame_index': frame_index, 'time_s': round(frame_index / fps, 6),
                       'width': w, 'height': h, 'objects': objects,
                       'processing_ms': round(elapsed, 3), 'hardware_synchronized': False}
                tracks.write(json.dumps(row, ensure_ascii=False) + '\n')
                for obj in objects:
                    tid = obj['id']
                    if tid not in lifetimes:
                        lifetimes[tid] = {'first_frame': frame_index, 'last_frame': frame_index,
                                          'observed_frames': 0, 'classes': Counter()}
                    life = lifetimes[tid]
                    life['last_frame'] = frame_index
                    life['observed_frames'] += 1
                    life['classes'][obj['class_name']] += 1
                    counts[obj['class_name']] += 1
                annotated = annotate(frame, objects, frame_index / fps,
                                     'PC CUDA' if args.device != 'cpu' else 'PC CPU')
                ff.stdin.write(annotated.tobytes())
                if processed % max(1, round(fps * 3)) == 0:
                    cv2.imwrite(str(run / f'frame_{frame_index:06d}.jpg'), annotated)
                    print(json.dumps({'frame': frame_index, 'time_s': row['time_s'],
                                      'visible': len(objects), 'processing_ms': row['processing_ms']}), flush=True)
                processed += 1
                ok, frame = cap.read()
                if not ok:
                    break
    finally:
        cap.release()
        ff.stdin.close()
        encode_exit = ff.wait(timeout=120)
    if encode_exit:
        raise RuntimeError('H264 encode failed; see encode.log')
    pipeline_wall_s = time.perf_counter() - wall_start
    if not processed:
        raise RuntimeError('No frames processed')
    decode = subprocess.run([str(args.ffmpeg), '-v', 'error', '-i', str(output),
                             '-f', 'null', '-'], capture_output=True, timeout=120)
    if decode.returncode:
        raise RuntimeError('Full output decode failed: ' + decode.stderr.decode(errors='replace'))
    summary = {'created_local': datetime.now().astimezone().isoformat(),
               'detector': 'Ultralytics YOLOv5su COCO', 'tracker': 'ByteTrack',
               'is_tommao_psp_model': False, 'semantic_segmentation_enabled': False,
               'hardware_access': False, 'board_firmware_changed': False,
               'hardware_synchronized': False, 'real_camera_input': False,
               'source': str(args.source), 'source_sha256': sha256(args.source),
               'weights': str(args.weights), 'weights_sha256': sha256(args.weights),
               'torch': torch.__version__, 'cuda_build': torch.version.cuda,
               'ultralytics': ultralytics.__version__, 'device': gpu_name,
               'imgsz': args.imgsz, 'classes': args.classes,
               'source_width': w, 'source_height': h, 'source_fps': fps,
               'start_frame': start_frame, 'processed_frames': processed,
               'video_duration_s': processed / fps,
               'wall_processing_fps': processed / pipeline_wall_s,
               'compute_ms_median': float(np.median(timing_ms)),
               'compute_ms_p95': float(np.percentile(timing_ms, 95)),
               'lost_buffer_seconds': args.lost_seconds,
               'unique_ids': len(lifetimes), 'track_lifetimes': lifetimes,
               'detection_frame_observations_by_class': counts,
               'full_output_decode_passed': True, 'output_sha256': sha256(output),
               'claim_limits': 'No dense ground truth, IDF1, real-camera latency, speed or hardware-synchronization claim.'}
    (run / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'output': str(output), 'frames': processed,
                      'median_compute_ms': summary['compute_ms_median'],
                      'p95_compute_ms': summary['compute_ms_p95'], 'device': gpu_name}), flush=True)


if __name__ == '__main__':
    main()
