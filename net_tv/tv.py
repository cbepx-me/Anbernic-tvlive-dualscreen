#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time
import json
import glob
import struct
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import ctypes
import logging
import select
import zipfile
import fcntl
import configparser

VERSION = "1.0.1"

# ============================
# 依赖检查与导入
# ============================
def ensure_deps():
    try:
        program = os.path.dirname(os.path.abspath(__file__))
        depspath = os.path.join(program, "deps")
        if not os.path.exists(depspath):
            module_file = os.path.join(program, "module.zip")
            if os.path.exists(module_file):
                with zipfile.ZipFile(module_file, 'r') as zip_ref:
                    zip_ref.extractall(program)
                print("Successfully installed deps")
        return True
    except Exception as e:
        print(f"Failed to install deps: {e}")
        return False

if ensure_deps():
    base_path = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(base_path, "deps"))

try:
    import sdl2
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Failed to import SDL2 and PIL modules. Please install them.")
    sys.exit(1)

# ============================
# 配置 & 常量
# ============================
APP_PATH = os.path.dirname(os.path.abspath(__file__))
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(levelname)s - %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
LOGGER = logging.getLogger("tv_dualscreen")
LOGGER.info("=== TVLive DualScreen Started ===")

class TVConfig:
    BOARD_MAPPING = {
        "RGds": 10,
        "RGdsplus": 11
    }

    COLOR_BG = "#0C121C"
    COLOR_BG_GRADIENT = "#182038"
    COLOR_TEXT = "#E0E8F0"
    COLOR_SHADOW = "#00000080"

    font_file = os.path.join(APP_PATH, "font", "font.ttf")
    if not os.path.exists(font_file):
        font_file = "/mnt/vendor/bin/default.ttf"

    KEYMAP = {
        304: "A", 305: "B", 306: "Y", 307: "X",
        308: "L1", 309: "R1", 314: "L2", 315: "R2",
        17: "DY", 16: "DX",
        310: "SELECT", 311: "START", 312: "MENUF",
        114: "V-", 115: "V+",
    }

    @staticmethod
    def screen_resolutions() -> Dict[int, Tuple[int, int, int]]:
        return {
            9: (1024, 768, 18),
            10: (640, 480, 11),
            11: (682, 512, 11)
        }

# ============================
# 多语言翻译器
# ============================
class Translator:
    def __init__(self, lang_code="en_US"):
        self.lang_code = lang_code
        self.lang_data = {}
        self.load_language(lang_code)

    def load_language(self, lang_code):
        base = os.path.dirname(os.path.abspath(__file__))
        lang_file = os.path.join(base, "lang", f"{lang_code}.json")
        if not os.path.exists(lang_file):
            lang_file = os.path.join(base, "lang", "en_US.json")
            LOGGER.warning("Language file %s not found, using en_US", lang_code)
        try:
            with open(lang_file, 'r', encoding='utf-8') as f:
                self.lang_data = json.load(f)
            LOGGER.info("Loaded language: %s", lang_code)
        except Exception as e:
            LOGGER.error("Failed to load language file: %s", e)
            self.lang_data = {}

    def t(self, key):
        return self.lang_data.get(key, key)

# ============================
# 输入处理
# ============================
class InputHandler:
    def __init__(self, cfg: TVConfig):
        self.cfg = cfg
        self.code_name = ""
        self.value = 0

        try:
            self.board_info = Path("/mnt/vendor/oem/board.ini").read_text().splitlines()[0]
        except:
            self.board_info = "RGds"
        self.device_path = self._find_anbernic_device()
        self.dev_fd = None

        try:
            self.dev_fd = open(self.device_path, "rb", buffering=0)
            flags = fcntl.fcntl(self.dev_fd, fcntl.F_GETFL)
            fcntl.fcntl(self.dev_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        except Exception as e:
            LOGGER.error("Failed to open input device: %s", e)
            self.dev_fd = None

    def _find_anbernic_device(self):
        keyword = "ANBERNIC"
        for event_path in glob.glob("/dev/input/event*"):
            dev_name = os.path.basename(event_path)
            sys_path = f"/sys/class/input/{dev_name}/device/name"
            try:
                with open(sys_path, 'r') as f:
                    name = f.read().strip()
                    if keyword in name:
                        return event_path
            except Exception:
                continue
        fallback = f"/dev/input/event{self.cfg.BOARD_MAPPING.get(self.board_info, 5)}"
        if os.path.exists(fallback):
            return fallback
        raise RuntimeError("No ANBERNIC input device found")

    def poll(self) -> None:
        if self.dev_fd is None:
            self.code_name = ""
            self.value = 0
            return
        try:
            rlist, _, _ = select.select([self.dev_fd], [], [], 0.01)
            if not rlist:
                self.code_name = ""
                self.value = 0
                return
            event = self.dev_fd.read(24)
            if not event:
                self.code_name = ""
                self.value = 0
                return
            (tv_sec, tv_usec, etype, kcode, kvalue) = struct.unpack("llHHI", event)
            if kvalue != 0:
                if kvalue != 1:
                    kvalue = -1
                else:
                    kvalue = 1
                self.code_name = self.cfg.KEYMAP.get(kcode, str(kcode))
                self.value = kvalue
                LOGGER.debug("Key: %s (code:%s val:%s)", self.code_name, kcode, kvalue)
            else:
                self.code_name = ""
                self.value = 0
        except Exception as e:
            LOGGER.error("Input error: %s", e)
            self.code_name = ""
            self.value = 0

    def is_key(self, name: str, key_value: int = 99) -> bool:
        if self.code_name == name:
            if key_value != 99:
                return self.value == key_value
            return True
        return False

    def slide_key(self) -> bool:
        return bool(self.code_name)

    def reset(self) -> None:
        self.code_name = ""
        self.value = 0

# ============================
# 触摸处理
# ============================
class TouchHandler:
    def __init__(self, device="/dev/input/event1", screen_width=640, screen_height=480):
        self.device = device
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.fd = None
        self.touch_active = False
        self.last_x = None
        self.last_y = None
        self.pending_tap = None
        self._open_device()

    def _open_device(self):
        try:
            self.fd = os.open(self.device, os.O_RDONLY | os.O_NONBLOCK)
            LOGGER.info(f"Touch device opened: {self.device}")
        except Exception as e:
            LOGGER.error(f"Failed to open touch device {self.device}: {e}")
            self.fd = None

    def get_tap(self):
        if self.fd is None:
            return None
        import select
        r, _, _ = select.select([self.fd], [], [], 0)
        if not r:
            return None
        while True:
            try:
                data = os.read(self.fd, 256)
                if len(data) == 0:
                    break
                offset = 0
                while offset + 24 <= len(data):
                    chunk = data[offset:offset+24]
                    sec, usec, etype, code, value = struct.unpack("llHHI", chunk)
                    if etype == 0x03:  # EV_ABS
                        if code == 0x35:  # ABS_MT_POSITION_X
                            self.last_x = value
                        elif code == 0x36:  # ABS_MT_POSITION_Y
                            self.last_y = value
                    elif etype == 0x01 and code == 0x14a:  # EV_KEY, BTN_TOUCH
                        new_active = (value == 1)
                        if not new_active and self.touch_active:
                            if self.last_x is not None and self.last_y is not None:
                                x = max(0, min(self.last_x, self.screen_width-1))
                                y = max(0, min(self.last_y, self.screen_height-1))
                                self.pending_tap = (x, y)
                        self.touch_active = new_active
                    offset += 24
            except BlockingIOError:
                break
            except Exception as e:
                LOGGER.error(f"Touch read error: {e}")
                break
        if self.pending_tap is not None:
            tap = self.pending_tap
            self.pending_tap = None
            return tap
        return None

    def set_screen_size(self, width, height):
        self.screen_width = width
        self.screen_height = height

# ============================
# 双屏 UI 渲染器
# ============================
class DualUIRenderer:
    def __init__(self, cfg: TVConfig, board_info=None):
        self.cfg = cfg
        self.board_info = board_info
        self.screens = []          # 每个元素: dict {window, renderer, surface, draw, width, height}
        self.current_target = 0    # 默认目标索引

        if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
            raise RuntimeError("SDL_Init failed")

        display_count = sdl2.SDL_GetNumVideoDisplays()
        LOGGER.info(f"Detected {display_count} displays")

        # 决定要创建的屏幕数量（最多2个）
        num_screens = min(display_count, 2)
        if num_screens < 1:
            raise RuntimeError("No display available")

        for disp_idx in range(num_screens):
            bounds = sdl2.SDL_Rect()
            if sdl2.SDL_GetDisplayBounds(disp_idx, bounds) == 0:
                w, h = bounds.w, bounds.h
            else:
                # 降级使用配置尺寸
                res = TVConfig.screen_resolutions().get(
                    cfg.BOARD_MAPPING.get(board_info or "RGds", 10), (640, 480, 11)
                )
                w, h = res[0], res[1]
            # 为 RGdsplus 特殊处理（如果需要）
            if board_info == "RGdsplus":
                res = TVConfig.screen_resolutions().get(cfg.BOARD_MAPPING.get("RGdsplus", 11), (682, 512, 11))
                w, h = res[0], res[1]

            # 创建窗口
            win = sdl2.SDL_CreateWindow(
                f"TVLive Screen {disp_idx}".encode(),
                sdl2.SDL_WINDOWPOS_CENTERED_DISPLAY(disp_idx),
                sdl2.SDL_WINDOWPOS_CENTERED_DISPLAY(disp_idx),
                w, h,
                sdl2.SDL_WINDOW_FULLSCREEN_DESKTOP | sdl2.SDL_WINDOW_SHOWN
            )
            if not win:
                LOGGER.error(f"Failed to create window on display {disp_idx}")
                continue

            ren = sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_ACCELERATED)
            if not ren:
                ren = sdl2.SDL_CreateRenderer(win, -1, sdl2.SDL_RENDERER_SOFTWARE)
            if not ren:
                sdl2.SDL_DestroyWindow(win)
                LOGGER.error(f"Failed to create renderer on display {disp_idx}")
                continue

            surface = Image.new("RGBA", (w, h), color=cfg.COLOR_BG)
            draw = ImageDraw.Draw(surface)

            self.screens.append({
                "window": win,
                "renderer": ren,
                "surface": surface,
                "draw": draw,
                "width": w,
                "height": h,
                "display": disp_idx
            })
            LOGGER.info(f"Screen {disp_idx} created ({w}x{h})")

        if not self.screens:
            raise RuntimeError("No screens created")

        # 默认目标：如果有两个屏幕，设为下屏（索引1），否则为0
        if len(self.screens) >= 2:
            self.current_target = 1   # 下屏作为 UI 目标
        else:
            self.current_target = 0

        LOGGER.info(f"Default target set to screen {self.current_target}")

    def set_target(self, screen_idx: int):
        if 0 <= screen_idx < len(self.screens):
            self.current_target = screen_idx
            LOGGER.debug(f"Drawing target switched to screen {screen_idx}")
        else:
            LOGGER.warning(f"Invalid screen index {screen_idx}, keeping current")

    def get_screen_size(self, screen_idx=None) -> Tuple[int, int]:
        if screen_idx is None:
            screen_idx = self.current_target
        if screen_idx < len(self.screens):
            return (self.screens[screen_idx]["width"], self.screens[screen_idx]["height"])
        return (640, 480)

    def clear(self, screen_idx=None):
        if screen_idx is None:
            screen_idx = self.current_target
        scr = self.screens[screen_idx]
        w, h = scr["width"], scr["height"]
        scr["surface"].paste(self.cfg.COLOR_BG, (0, 0, w, h))
        scr["draw"] = ImageDraw.Draw(scr["surface"])

    def text(self, pos, text, font_size=22, color=None, anchor=None, bold=False, shadow=False, screen_idx=None):
        if screen_idx is None:
            screen_idx = self.current_target
        scr = self.screens[screen_idx]
        color = color or self.cfg.COLOR_TEXT
        font_path = self.cfg.font_file
        try:
            fnt = ImageFont.truetype(font_path, font_size)
            if shadow:
                for dx, dy in [(1,1),(1,-1),(-1,1),(-1,-1)]:
                    scr["draw"].text((pos[0]+dx, pos[1]+dy), text, font=fnt, fill=self.cfg.COLOR_SHADOW, anchor=anchor)
            scr["draw"].text(pos, text, font=fnt, fill=color, anchor=anchor)
        except:
            fnt = ImageFont.load_default()
            scr["draw"].text(pos, text, font=fnt, fill=color, anchor=anchor)

    def rect(self, xy, fill=None, outline=None, width=1, radius=0, shadow=False, screen_idx=None):
        if screen_idx is None:
            screen_idx = self.current_target
        scr = self.screens[screen_idx]
        if shadow and radius > 0:
            sh_xy = [xy[0]+2, xy[1]+2, xy[2]+2, xy[3]+2]
            scr["draw"].rounded_rectangle(sh_xy, radius=radius, fill=self.cfg.COLOR_SHADOW)
        if radius > 0:
            scr["draw"].rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)
        else:
            scr["draw"].rectangle(xy, fill=fill, outline=outline, width=width)

    def circle(self, center, radius, fill=None, outline=None, shadow=False, screen_idx=None):
        if screen_idx is None:
            screen_idx = self.current_target
        scr = self.screens[screen_idx]
        x, y = center
        if shadow:
            scr["draw"].ellipse([x-radius-2, y-radius-2, x+radius+2, y+radius+2],
                                fill=self.cfg.COLOR_SHADOW)
        scr["draw"].ellipse([x-radius, y-radius, x+radius, y+radius], fill=fill, outline=outline)

    def blend_colors(self, color1: str, color2: str, ratio: float) -> str:
        r1, g1, b1 = int(color1[1:3], 16), int(color1[3:5], 16), int(color1[5:7], 16)
        r2, g2, b2 = int(color2[1:3], 16), int(color2[3:5], 16), int(color2[5:7], 16)
        r = int(r1 + (r2 - r1) * ratio)
        g = int(g1 + (g2 - g1) * ratio)
        b = int(b1 + (b2 - b1) * ratio)
        return f"#{r:02x}{g:02x}{b:02x}"

    def paint(self, screen_idx=None):
        if screen_idx is None:
            screen_idx = self.current_target
        scr = self.screens[screen_idx]
        img = scr["surface"]
        if img.mode != "RGBA":
            img = img.convert("RGBA")
        data = img.tobytes()
        w, h = scr["width"], scr["height"]
        surface = sdl2.SDL_CreateRGBSurfaceWithFormatFrom(
            data, w, h, 32, w * 4, sdl2.SDL_PIXELFORMAT_RGBA32
        )
        texture = sdl2.SDL_CreateTextureFromSurface(scr["renderer"], surface)
        sdl2.SDL_FreeSurface(surface)

        win_w = ctypes.c_int()
        win_h = ctypes.c_int()
        sdl2.SDL_GetWindowSize(scr["window"], ctypes.byref(win_w), ctypes.byref(win_h))
        dst = sdl2.SDL_Rect(0, 0, win_w.value, win_h.value)
        sdl2.SDL_RenderCopy(scr["renderer"], texture, None, dst)
        sdl2.SDL_RenderPresent(scr["renderer"])
        sdl2.SDL_DestroyTexture(texture)

    def paint_all(self):
        for i in range(len(self.screens)):
            self.paint(i)

    def draw_end(self):
        for scr in self.screens:
            sdl2.SDL_DestroyRenderer(scr["renderer"])
            sdl2.SDL_DestroyWindow(scr["window"])
        sdl2.SDL_Quit()

# ============================
# 直播源扫描与解析
# ============================
class TVScanner:
    @staticmethod
    def find_sources() -> List[Dict]:
        bases = [
            "/roms/TV", "/mnt/mmc/TV", "/mnt/sdcard/TV"
        ]
        bases.append(os.path.join(APP_PATH, "TV"))

        sources = []
        seen = set()
        for b in bases:
            if not os.path.isdir(b):
                continue
            for f in os.listdir(b):
                if f.endswith((".m3u", ".m3u8", ".txt")):
                    path = os.path.join(b, f)
                    real = os.path.realpath(path)
                    if real in seen:
                        continue
                    seen.add(real)
                    sources.append({"file": path, "name": os.path.splitext(f)[0]})
                    LOGGER.info("Found source: %s", f)
        return sources

    @staticmethod
    def parse_m3u(filepath: str) -> List[Dict]:
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except:
            return []

        raw = []
        cur_name = None
        cur_group = "未分组"
        cur_logo = ""

        for line in content.splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                if line.startswith("#EXTINF"):
                    g = line.split('group-title="')
                    if len(g) > 1:
                        cur_group = g[1].split('"')[0]
                    logo = line.split('tvg-logo="')
                    if len(logo) > 1:
                        cur_logo = logo[1].split('"')[0]
                    tn = line.split('tvg-name="')
                    if len(tn) > 1:
                        cur_name = tn[1].split('"')[0]
                    else:
                        if ',' in line:
                            cur_name = line.split(',')[-1].strip()
                continue
            if line.startswith("http"):
                if cur_name:
                    raw.append({
                        "name": cur_name,
                        "url": line,
                        "group": cur_group,
                        "logo": cur_logo
                    })
                cur_name = None
        name_counts = {}
        for c in raw:
            name_counts[c["name"]] = name_counts.get(c["name"], 0) + 1
        seen = {}
        for c in raw:
            name = c["name"]
            seen[name] = seen.get(name, 0) + 1
            c["lineNo"] = seen[name]
            c["lineTotal"] = name_counts[name]
        return raw

    @staticmethod
    def cache_path(source_file: str, source_name: str) -> str:
        safe = source_name.replace(" ", "_").replace("/", "_")
        return os.path.join(APP_PATH, f".cache_{safe}.dat")

    @staticmethod
    def save_cache(source: Dict, channels: List[Dict]):
        cp = TVScanner.cache_path(source["file"], source["name"])
        try:
            with open(cp, 'w') as f:
                f.write(source["file"] + "\n")
                f.write(str(os.path.getmtime(source["file"])) + "\n")
                f.write(str(len(channels)) + "\n")
                for c in channels:
                    f.write(f"{c['name']}\t{c['url']}\t{c['group']}\t{c['lineNo']}\t{c['lineTotal']}\t{c.get('logo','')}\n")
            LOGGER.info("Cache saved: %s (%d channels)", source["name"], len(channels))
        except Exception as e:
            LOGGER.error("Failed to save cache: %s", e)

    @staticmethod
    def load_cache(source: Dict) -> Optional[List[Dict]]:
        cp = TVScanner.cache_path(source["file"], source["name"])
        if not os.path.exists(cp):
            return None
        try:
            with open(cp, 'r') as f:
                cached_path = f.readline().strip()
                cached_time = float(f.readline().strip())
                count = int(f.readline().strip())
                cur_time = os.path.getmtime(source["file"])
                if cached_path != source["file"] or abs(cur_time - cached_time) > 1:
                    return None
                channels = []
                for _ in range(count):
                    parts = f.readline().strip().split('\t')
                    if len(parts) >= 5:
                        channels.append({
                            "name": parts[0],
                            "url": parts[1],
                            "group": parts[2],
                            "lineNo": int(parts[3]),
                            "lineTotal": int(parts[4]),
                            "logo": parts[5] if len(parts) > 5 else ""
                        })
                if len(channels) == count:
                    LOGGER.info("Cache hit: %s (%d channels)", source["name"], len(channels))
                    return channels
        except:
            pass
        return None

# ============================
# 播放器控制
# ============================
class TVPlayer:
    def __init__(self, video_display_index=0):
        self.pid = None
        self.proc = None
        self.status = "idle"
        self.fail_reason = ""
        self.volume = 60
        self.last_url = ""
        self.last_play_time = 0
        self.video_display = video_display_index
        self.mpv_log_file = "/tmp/mpv_tv.log"
        self.ipc_socket = "/tmp/mpv-ipc.sock"
        if os.path.exists(self.ipc_socket):
            os.remove(self.ipc_socket)

        self.volume_control = self._detect_volume_control()
        LOGGER.info("Selected ALSA volume control: %s", self.volume_control)
        self._enable_outputs()

    def _list_controls(self) -> List[str]:
        try:
            out = subprocess.check_output(["amixer", "scontrols"], stderr=subprocess.DEVNULL).decode()
            controls = [line.split("'")[1] for line in out.splitlines() if "Simple mixer control" in line]
            return controls
        except Exception as e:
            LOGGER.error("Failed to list controls: %s", e)
            return []

    def _detect_volume_control(self) -> Optional[str]:
        controls = self._list_controls()
        if not controls:
            return None
        for cand in ["lineout volume", "digital volume", "Master", "PCM", "Playback"]:
            if cand in controls:
                try:
                    info = subprocess.check_output(["amixer", "sget", cand], stderr=subprocess.DEVNULL).decode()
                    if "volume" in info.lower() or "%" in info:
                        return cand
                except:
                    pass
        for c in controls:
            if "volume" in c.lower():
                return c
        return controls[0] if controls else None

    def _enable_outputs(self):
        for ctrl in ["LINEOUT", "SPK"]:
            try:
                subprocess.run(["amixer", "set", ctrl, "on"], stderr=subprocess.DEVNULL, check=False)
            except:
                pass
        LOGGER.info("Outputs enabled (LINEOUT, SPK)")

    def set_volume_remote(self, vol):
        vol = max(0, min(130, vol))
        if self.proc is None or self.proc.poll() is not None:
            # mpv 未运行，仅更新内部变量
            self.volume = vol
            return
        try:
            import socket
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            sock.connect(self.ipc_socket)
            cmd = json.dumps({"command": ["set_property", "volume", vol]})
            sock.sendall((cmd + "\n").encode())
            # 读取响应（可选）
            sock.close()
            self.volume = vol
            LOGGER.info("Volume set to %d via IPC", vol)
        except Exception as e:
            LOGGER.error("IPC volume set failed: %s", e)

    
    def play(self, url: str, sub_file=None):
        now = time.time()
        if url == self.last_url and now - self.last_play_time < 1.0:
            LOGGER.info("防连发跳过: %s", url)
            return
        self.stop()
        self.status = "connecting"
        self.fail_reason = ""
        self.last_url = url
        self.last_play_time = now
        mpv_vol = min(130, self.volume)
        sub = ""
        if sub_file is not None:
            sub = f"--sub-file={sub_file}"

        cmd = [
            "mpv",
            "--fullscreen",
            "--fs-screen=1",          # 固定上屏
            "--vo=gpu",               # 已验证有效的驱动
            f"--volume={mpv_vol}", sub,
            "--network-timeout=20", "--cache=yes", "--cache-secs=10",
            "--stream-lavf-o=reconnect=1",
            "--user-agent=Mozilla/5.0",
            f"--log-file={self.mpv_log_file}",
            "--msg-level=all=debug",
            f"--input-ipc-server={self.ipc_socket}",
            url
        ]
        # 移除空参数
        cmd = [c for c in cmd if c != ""]

        LOGGER.info("Playing on upper screen (fs-screen=1)")
        ret, err = self._launch_mpv(cmd, None)
        if ret == 0:
            LOGGER.info("Playback started successfully")
        else:
            self.status = "failed"
            self.fail_reason = f"mpv exit {ret} (see log)"
            self._print_mpv_log()
            LOGGER.error("Playback failed")

    def _launch_mpv(self, cmd, env=None) -> Tuple[int, str]:
        try:
            if os.path.exists(self.mpv_log_file):
                os.remove(self.mpv_log_file)
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
            time.sleep(1.5)
            ret = proc.poll()
            if ret is not None:
                err_output = proc.stderr.read().decode('utf-8', errors='ignore')
                return ret, err_output
            else:
                self.proc = proc
                self.pid = proc.pid
                self.status = "playing"
                return 0, ""
        except Exception as e:
            return -1, str(e)

    def _print_mpv_log(self):
        if os.path.exists(self.mpv_log_file):
            try:
                with open(self.mpv_log_file, 'r') as f:
                    content = f.read()
                if content:
                    LOGGER.error("mpv log (full):\n%s", content)
                else:
                    LOGGER.warning("mpv log file is empty")
            except Exception as e:
                LOGGER.error("Failed to read mpv log: %s", e)
        else:
            LOGGER.warning("mpv log file not found")

    def stop(self):
        if self.pid:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=1)
            except:
                self.proc.kill()
            self.pid = None
            self.proc = None
            self.status = "idle"
            LOGGER.info("Stopped")

    def is_alive(self) -> bool:
        if self.proc is not None:
            ret = self.proc.poll()
            if ret is None:
                return True
            self.proc = None
            self.pid = None
            return False
        return False

# ============================
# 主应用
# ============================
class TVApp:
    def __init__(self):
        self.cfg = TVConfig()
        try:
            board_info = Path("/mnt/vendor/oem/board.ini").read_text().splitlines()[0]
        except:
            board_info = "RGds"
        self.board_info = board_info
        self.hw_info = self.cfg.BOARD_MAPPING.get(board_info, 5)

        # 检测显示器数量，决定UI屏幕和视频屏幕
        sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO)
        display_count = sdl2.SDL_GetNumVideoDisplays()
        sdl2.SDL_Quit()
        LOGGER.info(f"Detected {display_count} displays")
        if display_count >= 2:
            self.ui_display = 1      # 下屏（假设索引1为触摸屏）
            self.video_display = 0   # 上屏
        else:
            self.ui_display = 0
            self.video_display = 0
            LOGGER.warning("Only one display, UI and video will share the same screen (video may overlap)")

        # 初始化UI渲染器（下屏）
        self.ui = DualUIRenderer(self.cfg, self.board_info)
        w, h = self.ui.get_screen_size()
        self.touch = TouchHandler(screen_width=w, screen_height=h)

        self.input = InputHandler(self.cfg)
        self.player = TVPlayer(self.video_display)

        self.sources = []
        self.source_cache = {}
        self.current_source_idx = 0
        self.current_channel_idx = 0
        self.current_channels = []
        self.playing_channel_idx = -1

        self.wifi_connected = False
        self.wifi_essid = ""
        self.battery_level = 0
        self.battery_charging = False
        self.status_timer = 0.0
        self.now_sec = 0.0
        self.fullscreen = False
        self.show_hints = True
        self.last_show_hints = not self.show_hints
        self.hint_timer_default = 10.0
        self.reset_hint_timer()

        self.system_langs = ("zh_CN", "zh_TW", "en_US", "ja_JP", "ko_KR", "es_LA", "ru_RU", "de_DE", "fr_FR", "pt_BR")

        self.config = configparser.ConfigParser()
        self.config_file = os.path.join(APP_PATH, "tv.ini")
        self._load_config()
        self.language = self._load_language()
        self.translator = Translator(self.language)

        self._ready()
        self._scan_sources()
        self._update_system_status()

        self.skip_first_input = True

    def reset_hint_timer(self):
        self.hint_timer = self.hint_timer_default

    def t(self, key):
        return self.translator.t(key)

    def _load_language(self):
        if hasattr(self, 'config'):
            lang = self.config.get('General', 'language', fallback=None)
            if lang and lang in self.system_langs:
                return lang
        try:
            lang_path = "/mnt/vendor/oem/language.ini"
            if os.path.exists(lang_path):
                with open(lang_path, 'r') as f:
                    idx = int(f.read().strip())
                    if 0 <= idx < len(self.system_langs):
                        return self.system_langs[idx]
        except:
            pass
        return "en_US"

    def _save_language(self, lang):
        self.config['General']['language'] = lang
        self._save_config()

    def _ready(self):
        target_path = [
            "/mnt/mmc",
            "/mnt/sdcard",
            APP_PATH
        ]
        for path in target_path:
            if path == "/mnt/sdcard" and not os.path.ismount(path):
                continue
            tv_dir = os.path.join(path, "TV")
            os.makedirs(tv_dir, exist_ok=True)

    def _load_config(self):
        if os.path.exists(self.config_file):
            self.config.read(self.config_file)
        for sec in ['General', 'Volume', 'Resume']:
            if sec not in self.config:
                self.config[sec] = {}

        vol = self.config.getint('Volume', 'value', fallback=80)
        vol = max(0, min(130, vol))
        self.player.set_volume_remote(vol)

        src = self.config.getint('Resume', 'source_index', fallback=1)
        ch = self.config.getint('Resume', 'channel_index', fallback=1)
        self.current_source_idx = src - 1
        self.current_channel_idx = ch - 1

    def _save_config(self):
        self.config['Volume']['value'] = str(self.player.volume)
        self.config['Resume']['source_index'] = str(self.current_source_idx + 1)
        self.config['Resume']['channel_index'] = str(self.current_channel_idx + 1)
        try:
            with open(self.config_file, 'w') as f:
                self.config.write(f)
        except Exception as e:
            LOGGER.error("Failed to save config: %s", e)

    def _scan_sources(self):
        self.sources = TVScanner.find_sources()
        self.source_cache = {}
        if self.sources:
            if self.current_source_idx < 0 or self.current_source_idx >= len(self.sources):
                self.current_source_idx = 0
            self._load_source(self.current_source_idx)
        else:
            self.current_channels = []
            self.current_source_idx = 0
            LOGGER.warning("No TV sources found")

    def _load_source(self, idx: int):
        if idx < 0 or idx >= len(self.sources):
            return
        src = self.sources[idx]
        ch = TVScanner.load_cache(src)
        if ch is None:
            ch = TVScanner.parse_m3u(src['file'])
            if ch:
                TVScanner.save_cache(src, ch)
        self.current_source_idx = idx
        self.current_channels = ch
        if self.current_channel_idx < 0 or self.current_channel_idx >= len(ch):
            self.current_channel_idx = 0
        LOGGER.info("Loaded %s: %d channels", src['name'], len(ch))

    def _update_system_status(self):
        try:
            with open("/sys/class/net/wlan0/operstate", 'r') as f:
                op = f.read().strip()
            self.wifi_connected = (op == "up")
            if not self.wifi_connected:
                with open("/proc/net/wireless", 'r') as f:
                    w = f.read().strip().split()
                    if "wlan" in w:
                        self.wifi_connected = True
            if self.wifi_connected:
                essid = subprocess.check_output(["iw", "dev", "wlan0", "link"], stderr=subprocess.DEVNULL).decode()
                for line in essid.splitlines():
                    if line.strip().startswith('SSID'):
                        self.wifi_essid = line.split('SSID:', 1)[1].strip()
            else:
                self.wifi_essid = ""
        except:
            self.wifi_connected = False
            self.wifi_essid = ""

        for p in ["battery", "BAT0", "axp2202-battery"]:
            try:
                with open(f"/sys/class/power_supply/{p}/capacity", 'r') as f:
                    self.battery_level = int(f.read().strip())
                try:
                    with open(f"/sys/class/power_supply/{p}/status", 'r') as f:
                        st = f.read().strip()
                        self.battery_charging = (st in ["Charging", "Full"])
                except:
                    self.battery_charging = False
                break
            except:
                continue

    def handle_input(self):
        if self.skip_first_input:
            self.input.reset()
            self.skip_first_input = False
            return
        
        self.input.poll()
        if not self.input.code_name:
            return

        k = self.input.code_name
        val = self.input.value

        # 公共退出键
        if k == "MENUF":
            self.quit()
            return

        # 音量调节（无论亮屏熄屏都工作）
        if k == "V-":
            new_vol = self.player.volume - 5
            self.player.set_volume_remote(new_vol)
            self._save_config()
            self.reset_hint_timer()
            return
        if k == "V+":
            new_vol = self.player.volume + 5
            self.player.set_volume_remote(new_vol)
            self._save_config()
            self.reset_hint_timer()
            return

        # X 键切换 UI 显示（亮屏/熄屏）
        if k == "X":
            self.show_hints = not self.show_hints
            self.reset_hint_timer()
            self.input.reset()
            return

        # 以下按键仅在亮屏（show_hints=True）时完全响应，熄屏时只处理特殊逻辑
        if not self.show_hints:
            # 熄屏模式：只响应少数按键（切换源/停止等），并自动恢复亮屏
            if k == "B":
                self.show_hints = True   # 唤醒 UI
                self.reset_hint_timer()
                self.input.reset()
                return
            elif k in ["DX", "DY"]:
                pass
            else:
                self.input.reset()
                return

        # ===== 以下为亮屏状态（或刚唤醒）的完整按键处理 =====
        if k == "L1":
            self.change_source(-1)
            self.reset_hint_timer()
            if self.player.status == "playing":
                self.fullscreen = False
                self.player.stop()
            if self.player.status == "failed":
                self.player.status = "idle"
        elif k == "R1":
            self.change_source(1)
            self.reset_hint_timer()
            if self.player.status == "playing":
                self.fullscreen = False
                self.player.stop()
            if self.player.status == "failed":
                self.player.status = "idle"
        elif k == "B":
            self.fullscreen = False
            self.reset_hint_timer()
            if self.player.status == "playing":
                self.player.stop()
                self.playing_channel_idx = -1
        elif k == "A":
            self.reset_hint_timer()
            if self.player.status != "playing" or self.playing_channel_idx != self.current_channel_idx:
                self.fullscreen = True
                sub_file = self.make_sub()
                self.play_selected(sub_file)
        elif k == "Y":
            self.reset_hint_timer()
            self._scan_sources()
            if self.player.status == "failed":
                self.player.status = "idle"
        elif k == "DY":
            self.reset_hint_timer()
            if val == -1:
                self.change_channel(-1)
            elif val == 1:
                self.change_channel(1)
            if self.player.status == "failed":
                self.player.status = "idle"
        elif k == "DX":
            self.reset_hint_timer()
            page = self._page_size()
            if val == -1:
                self.change_channel(-page)
            elif val == 1:
                self.change_channel(page)
            if self.player.status == "failed":
                self.player.status = "idle"
        elif k == "SELECT":
            self.reset_hint_timer()
            langs = self.system_langs
            try:
                idx = langs.index(self.language)
                self.language = langs[(idx + 1) % len(langs)]
            except:
                self.language = langs[0]
            self.translator.load_language(self.language)
            self._save_language(self.language)

        self.input.reset()

    def change_channel(self, delta: int):
        if not self.current_channels:
            return
        n = len(self.current_channels)
        self.current_channel_idx = (self.current_channel_idx + delta) % n
        LOGGER.info("Channel: %s", self.current_channels[self.current_channel_idx]['name'])
        if self.player.is_alive():
            sub_file = self.make_sub()
            self.play_selected(sub_file)

    def change_source(self, delta: int):
        if not self.sources:
            return
        n = len(self.sources)
        self.current_source_idx = (self.current_source_idx + delta) % n
        self._load_source(self.current_source_idx)
        self.current_channel_idx = 0
        if self.player.status == "playing":
            self.player.stop()
            self.playing_channel_idx = -1
        LOGGER.info("Source: %s", self.sources[self.current_source_idx]['name'])

    def play_selected(self, sub_file=None):
        if not self.current_channels or self.current_channel_idx >= len(self.current_channels):
            return
        ch = self.current_channels[self.current_channel_idx]
        self.player.play(ch['url'], sub_file)
        if self.player.status == "playing":
            self.playing_channel_idx = self.current_channel_idx
        else:
            self.playing_channel_idx = -1

    def _page_size(self) -> int:
        top_h = 52
        bottom_h = 52
        item_h = 30
        screen_idx = 1 if len(self.ui.screens) >= 2 else 0
        w, h = self.ui.get_screen_size(screen_idx)
        avail = h - top_h - bottom_h - 40
        return max(1, int(avail / item_h))

    def draw_upper_screen_standby(self, bright=True):
        if len(self.ui.screens) < 2:
            return

        self.ui.set_target(0)
        W, H = self.ui.get_screen_size(0)

        center_x = W // 2
        center_y = H // 2 - 20

        if bright:
            main_color = "#4FC3F7"
            sub_color = "#90CAF9"
        else:
            main_color = "#1C263B"
            sub_color = "#1E2636"

        self.ui.text((center_x, center_y), "Anbernic", font_size=72, color=main_color, anchor="mm", screen_idx=0)
        self.ui.text((center_x, center_y + 50), "TVLive", font_size=24, color=sub_color, anchor="mm", screen_idx=0)

        self.ui.paint(0)
        self.ui.set_target(1)

    def update(self, dt: float):
        self.now_sec += dt
        self.status_timer += dt
        if self.status_timer >= self.hint_timer_default:
            self.status_timer = 0
            self._update_system_status()

        if self.show_hints:
            self.hint_timer -= dt
            if self.hint_timer <= 0:
                self.show_hints = False

        if self.player.pid and not self.player.is_alive():
            self.player.pid = None
            self.player.proc = None
            if self.player.status != "idle":
                self.player.status = "failed"
                self.fullscreen = False
                self.playing_channel_idx = -1
                LOGGER.info("mpv exited")
        if self.show_hints != self.last_show_hints:
            self.last_show_hints = self.show_hints
            if self.player.status == "idle" and len(self.ui.screens) >= 2:
                self.draw_upper_screen_standby(bright=self.show_hints)
                self.upper_standby_drawn = True

    def make_sub(self):
        sub_file = os.path.join(APP_PATH, "tv.srt")
        ch = self.current_channels
        cur = ch[self.current_channel_idx] if ch else None
        if cur:
            if cur['lineTotal'] > 1:
                line_info = f"{self.t('Group')} {cur['group']}  |  {self.t('Line')} {cur['lineNo']}/{cur['lineTotal']}"
            else:
                line_info = f"{self.t('Group')} {cur['group']}"
            sub_txt = f"{cur['name']} ({line_info})"
            lines = [
                "1",
                "00:00:00,000 --> 00:00:05,000",
                sub_txt
            ]
            with open(sub_file, "w", encoding="utf-8") as f:
                for line in lines:
                    f.write(line + "\n")
        else:
            with open(sub_file, "w", encoding="utf-8") as f:
                f.write("")
        return sub_file

    def draw(self):
        ui = self.ui
        t = self.t
        W, H = ui.get_screen_size()

        ch = self.current_channels
        cur = ch[self.current_channel_idx] if ch else None

        ui.clear()
        top_h = 44
        bot_h = 44

        if not self.show_hints:
            ui.clear()
            ui.rect([0,0,W,H], fill="#000000C8")
            ui.text((W//2, H//2-30), f"{t('Screen off playing')} · {t('Press X/B to turn on')}", font_size=32, color="#3A3A3A", anchor="mm")
            ui.paint()
            return

        # ---------- 顶部状态栏 ----------
        ui.rect([0,0,W,top_h], fill="#0A1020")
        ui.text((12, 12), f'{t("TVLive")} v{VERSION}', font_size=18, color="#E0E8F0")
        bat_str = f"{self.battery_level}%" + (" █" if self.battery_charging else "")
        bat_color = "#4FC3F7" if self.battery_level >= 60 else "#64F6A6" if self.battery_level >= 20 else "#EF5350"
        ui.text((W-12, 20), bat_str, font_size=18, color=bat_color, anchor="rm")
        t_str = time.strftime("%H:%M")
        ui.text((W-12-80, 20), t_str, font_size=18, color="#E0E8F0", anchor="rm")
        wifi_str = f"{t('WiFi:')} {self.wifi_essid}" if self.wifi_connected else t("WiFi ×")
        wifi_color = "#4FC3F7" if self.wifi_connected else "#EF5350"
        ui.text((W-12-200, 20), wifi_str, font_size=18, color=wifi_color, anchor="rm")

        # ---------- 左侧列表 ----------
        lx, ly = 12, top_h + 8
        lw = int(W * 0.40)
        lh = H - ly - bot_h - 16
        ui.rect([lx, ly, lx+lw, ly+lh], fill="#0F1A2E", radius=8)
        ui.rect([lx+2, ly+2, lx+lw-2, ly+lh-2], fill=None, outline="#1E3A5F", radius=8, width=1)

        # 源信息
        src_name = self.sources[self.current_source_idx]['name'] if self.sources else t("No source")
        ui.text((lx+12, ly+8), f"● {src_name}  ({len(ch)}{t('channels')})", font_size=18, color="#64B5F6")
        ui.text((lx+12, ly+34), f"{self.current_source_idx+1}/{len(self.sources)} {t('Source')}", font_size=14, color="#7A8BA0")

        # 列表项
        item_h = 28
        list_top = ly + 54
        visible = self._page_size()
        start = self.current_channel_idx - visible//2
        if start < 0: start = 0
        if ch and start > len(ch) - visible:
            start = max(0, len(ch) - visible)

        for i in range(visible):
            idx = start + i
            if idx < len(ch):
                y = list_top + i * item_h
                is_sel = (idx == self.current_channel_idx)
                if is_sel:
                    ui.rect([lx+6, y, lx+lw-6, y+item_h-2], fill="#1E88E5", radius=4)
                    col = "#FFFFFF"
                else:
                    col = "#B0C4DE"
                name = ch[idx]['name']
                if len(name) > 20: name = name[:18]+"…"
                line_mark = f"  {ch[idx]['lineNo']}/{ch[idx]['lineTotal']}" if ch[idx]['lineTotal'] > 1 else ""
                label = f"{idx+1:2d} {name}{line_mark}"
                ui.text((lx+12, y+4), label, font_size=16, color=col)

        # ---------- 右侧主面板 ----------
        rx = lx + lw + 12
        rw = W - rx - 12
        rh = lh
        ui.rect([rx, ly, rx+rw, ly+rh], fill="#0F1A2E", radius=8)
        ui.rect([rx+2, ly+2, rx+rw-2, ly+rh-2], fill=None, outline="#1E3A5F", radius=8, width=1)

        if cur:
            name_display = cur['name']
            ui.text((rx+rw//2, ly+40), name_display, font_size=28, color="#E0E8F0", anchor="mm")
            ui.text((rx+rw//2, ly+80), f"{t('Group:')} {cur['group']}", font_size=16, color="#7A8BA0", anchor="mm")
            if cur['lineTotal'] > 1:
                ui.text((rx+rw//2, ly+110), f"{t('Line:')} {cur['lineNo']}/{cur['lineTotal']}", font_size=16, color="#7A8BA0", anchor="mm")
            url_short = cur['url'][:40] + "…" if len(cur['url']) > 40 else cur['url']
            ui.text((rx+rw//2, ly+140), url_short, font_size=14, color="#5A6A7F", anchor="mm")

            btn_w, btn_h = 200, 40
            btn_x = rx + (rw - btn_w)//2
            btn_y = ly + rh - 60
            ui.rect([btn_x, btn_y, btn_x+btn_w, btn_y+btn_h], fill="#1E3A5F", radius=8)
            ui.text((btn_x+btn_w//2, btn_y+btn_h//2), t("A Play"), font_size=18, color="#B0C4DE", anchor="mm")
        else:
            ui.text((rx+rw//2, ly+rh//2), t("No channels"), font_size=24, color="#7A8BA0", anchor="mm")

        if self.player.status in ("connecting", "playing", "failed"):
            status_txt = {
                "connecting": t("Connecting..."),
                "playing": t("Playing..."),
                "failed": t("Playback failed")
            }.get(self.player.status, "")
            color = "#66BB6A" if self.player.status == "playing" else "#FF6B6B"
            ui.text((rx+rw//2, ly+rh//2+20), status_txt, font_size=22, color=color, anchor="mm")

        # ---------- 底部提示 ----------
        hint1 = (
            " ↑↓ " + t("Channel") + "  ←→ " + t("Page") +
            "  L1/R1 " + t("Source") + " SEL " + t("Language")
        )
        hint2 = (
            "  A " + t("Play") +
            "  B " + t("Stop") + "  X " + t("screen off") +
            "  Y " + t("Refresh") + "  RG " + t("Exit")
        )
        ui.rect([0, H-bot_h, W, H], fill="#0A1020")
        ui.text((12, H-bot_h+1), hint1, font_size=16, color="#B0C4DE")
        ui.text((12, H-bot_h+23), hint2, font_size=16, color="#B0C4DE")

        # ---------- 音量显示条 ----------
        vol = self.player.volume
        vol_bar_width = 180
        vol_bar_height = 8
        vol_bar_x = rx + (rw - btn_w)//2
        vol_bar_y = ly+rh//2+70
        ui.rect([vol_bar_x, vol_bar_y, vol_bar_x + vol_bar_width, vol_bar_y + vol_bar_height],
                fill="#2A3A5A", radius=4)
        fill_width = int((vol / 130) * vol_bar_width)
        if fill_width > 0:
            if vol < 50:
                color = "#4CAF50"
            elif vol < 80:
                color = "#FFEB3B"
            else:
                color = "#FF5722"
            ui.rect([vol_bar_x, vol_bar_y, vol_bar_x + fill_width, vol_bar_y + vol_bar_height],
                    fill=color, radius=4)
        ui.text((vol_bar_x + vol_bar_width + 8, vol_bar_y + vol_bar_height//2),
                f"{vol}%", font_size=14, color="#B0C4DE", anchor="lm")
        ui.text((rx+rw//2, vol_bar_y + vol_bar_height+30),
                t("Volume"), font_size=16, color="#B0C4DE", anchor="mm")

        ui.paint()

    def quit(self):
        self._save_config()
        self.player.stop()
        self.ui.draw_end()
        sys.exit(0)

    # ---------- 主循环 ----------
    def run(self):
        last_time = time.time()
        while True:
            now = time.time()
            dt = min(0.05, now - last_time)
            last_time = now

            # 处理触摸点击（下屏）
            tap = self.touch.get_tap()
            if tap is not None:
                x, y = tap
                LOGGER.info(f"Touch at ({x},{y})")
                # 粗略判断点击区域：左侧列表点击切换频道，右侧按钮点击播放
                W, H = self.ui.get_screen_size()
                lw = int(W * 0.40)
                # 点击左侧列表区域切换频道
                if x < lw:
                    # 计算频道索引
                    top_h = 44
                    ly = top_h + 8
                    item_h = 28
                    list_top = ly + 54
                    visible = self._page_size()
                    start = self.current_channel_idx - visible//2
                    if start < 0: start = 0
                    for i in range(visible):
                        idx = start + i
                        if idx < len(self.current_channels):
                            rect_y = list_top + i * item_h
                            if rect_y <= y <= rect_y + item_h - 2:
                                self.current_channel_idx = idx
                                LOGGER.info("Touch selected channel %d", idx)
                                # 自动播放？可配置，这里仅切换
                                if self.player.is_alive():
                                    sub_file = self.make_sub()
                                    self.play_selected(sub_file)
                                break
                else:
                    # 右侧区域点击“播放”按钮
                    rx = lw + 12
                    rw = W - rx - 12
                    ly = top_h + 8
                    lh = H - ly - 44 - 16
                    btn_w, btn_h = 200, 40
                    btn_x = rx + (rw - btn_w)//2
                    btn_y = ly + lh - 60
                    if btn_x <= x <= btn_x+btn_w and btn_y <= y <= btn_y+btn_h:
                        if self.player.status != "playing" or self.playing_channel_idx != self.current_channel_idx:
                            sub_file = self.make_sub()
                            self.play_selected(sub_file)

            # 按键处理
            self.handle_input()
            self.update(dt)
            self.draw()
            time.sleep(0.02)

# ============================
# 入口
# ============================
if __name__ == "__main__":
    app = TVApp()
    try:
        app.run()
    except KeyboardInterrupt:
        app.quit()
    except Exception as e:
        LOGGER.exception("Unhandled exception")
        app.ui.draw_end()
        sys.exit(1)