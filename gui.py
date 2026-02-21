from __future__ import annotations

import atexit
import os
import logging
import signal
import subprocess
import threading
import tkinter as tk
from tkinter import ttk
import struct
import flatbuffers
from hibiki.ipc import Message, Command, LoadPlugin, LoadClip, Play, Stop, PlayClip, StopTrack, ShowPluginGui, SetParamValue, Quit
from typing import Any, Callable, Dict, List, Optional, Tuple


class Gui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()

        self.title("hibiki")
        self.geometry("1024x768")

        # Classic Ableton utilitarian color palette
        self.colors: Dict[str, str] = {
            "bg_dark": "#505050",
            "bg_mid": "#808080",
            "bg_light": "#A0A0A0",
            "bg_track": "#909090",
            "bg_display": "#D0D0D0", # For digital displays (BPM, Timestamp)
            "text_light": "#FFFFFF",
            "text_dark": "#000000",
            "btn_active": "#E0B050",
            "btn_mute": "#E06060",
            "btn_solo": "#60A0E0",
            "btn_arm": "#D06060",
            "btn_play": "#60E060"
        }

        self.configure(bg=self.colors["bg_dark"])

        # Track / Slot selection state
        self.selected_track: int = 1
        self.selected_slot: int = 0
        self.track_frames: Dict[int, tk.Frame] = {} # track_idx -> frame
        self.track_headers: Dict[int, tk.Label] = {} # track_idx -> label
        self.clip_buttons: Dict[Tuple[int, int], tk.Button] = {} # (track_idx, slot_idx) -> button

        self.track_plugins: Dict[int, List[Dict[str, Any]]] = {} # track_idx -> list of {path, name, params}
        self.selected_plugin_idx: int = -1

        # UI Elements
        self.status_label: tk.Label
        self.play_btn: tk.Button
        self.stop_btn: tk.Button
        self.main_paned: ttk.PanedWindow
        self.browser_frame: tk.Frame
        self.workspace_paned: ttk.PanedWindow
        self.session_frame: tk.Frame
        self.detail_frame: tk.Frame
        self.browser_tree: ttk.Treeview
        self.vst_node: str
        self.midi_node: str

        self.create_layout()

        # Backend process management
        self.backend: Optional[subprocess.Popen[str]] = None
        self.stderr_thread: Optional[threading.Thread] = None
        self.start_backend()
        atexit.register(self.stop_backend_process)


    def start_backend(self) -> None:
        from python.runfiles import runfiles
        r = runfiles.Create()
        assert r is not None
        backend_bin = r.Rlocation("hibiki/hbk-play")
        assert backend_bin is not None
        if not os.path.exists(backend_bin):
            self.status_label.config(text=f"Error: {backend_bin} not found. Build it first.")
            raise ValueError(f"Error: {backend_bin} not found. Build it first.")

        self.stop_backend_process()

        # Start backend in command mode (no initial arguments)
        self.backend = subprocess.Popen(
            [backend_bin],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        self.status_label.config(text="Backend started in multi-track mode.")

        # Start thread to monitor stderr
        self.stderr_thread = threading.Thread(target=self.monitor_backend_stderr, daemon=True)
        self.stderr_thread.start()

    def monitor_backend_stderr(self) -> None:
        while self.backend and self.backend.poll() is None:
            if self.backend.stderr:
                line: str = self.backend.stderr.readline()
                if line:
                    print(f"BACKEND ERROR: {line.strip()}")
                    def update_status(l: str = line) -> None:
                        self.status_label.config(text=f"Error: {l.strip()}")
                    self.after(0, update_status)
                else:
                    break
            else:
                break

    def _send_flatbuffer(self, builder: flatbuffers.Builder) -> None:
        if self.backend and self.backend.poll() is None:
            try:
                if self.backend.stdin:
                    data = builder.Output()
                    # Sent length-prefixed message (4 bytes, little endian)
                    self.backend.stdin.buffer.write(struct.pack("<I", len(data)))
                    self.backend.stdin.buffer.write(data)
                    self.backend.stdin.buffer.flush()
            except Exception as e:
                self.status_label.config(text=f"Command error: {e}")
        else:
            print("Backend not running.")
            try:
                if self.status_label.winfo_exists():
                    self.status_label.config(text="Backend not running.")
            except:
                pass

    def send_load_plugin(self, track_idx: int, path: str, plugin_idx: int = 0) -> None:
        builder = flatbuffers.Builder(1024)
        path_off = builder.CreateString(path)
        
        LoadPlugin.Start(builder)
        LoadPlugin.AddTrackIndex(builder, track_idx)
        LoadPlugin.AddPath(builder, path_off)
        LoadPlugin.AddPluginIndex(builder, plugin_idx)
        inst_off = LoadPlugin.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.LoadPlugin)
        Message.AddCommand(builder, inst_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

        # Optimistically track for UI
        if track_idx not in self.track_plugins:
            self.track_plugins[track_idx] = []
        self.track_plugins[track_idx].append({
            "path": path,
            "name": os.path.basename(path),
            "params": [] # We don't know them yet without backend feedback
        })
        self.after(0, self.refresh_detail_view)

    def send_set_param_value(self, track_idx: int, plugin_idx: int, param_id: int, value: float) -> None:
        builder = flatbuffers.Builder(1024)
        
        SetParamValue.Start(builder)
        SetParamValue.AddTrackIndex(builder, track_idx)
        SetParamValue.AddPluginIndex(builder, plugin_idx)
        SetParamValue.AddParamId(builder, param_id)
        SetParamValue.AddValue(builder, value)
        cmd_off = SetParamValue.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.SetParamValue)
        Message.AddCommand(builder, cmd_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_show_plugin_gui(self, track_idx: int, plugin_idx: int) -> None:
        builder = flatbuffers.Builder(1024)
        
        ShowPluginGui.Start(builder)
        ShowPluginGui.AddTrackIndex(builder, track_idx)
        ShowPluginGui.AddPluginIndex(builder, plugin_idx)
        cmd_off = ShowPluginGui.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.ShowPluginGui)
        Message.AddCommand(builder, cmd_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_load_clip(self, track_idx: int, slot_idx: int, path: str) -> None:
        builder = flatbuffers.Builder(1024)
        path_off = builder.CreateString(path)
        
        LoadClip.Start(builder)
        LoadClip.AddTrackIndex(builder, track_idx)
        LoadClip.AddSlotIndex(builder, slot_idx)
        LoadClip.AddPath(builder, path_off)
        clip_off = LoadClip.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.LoadClip)
        Message.AddCommand(builder, clip_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_play(self) -> None:
        builder = flatbuffers.Builder(128)
        Play.Start(builder)
        play_off = Play.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.Play)
        Message.AddCommand(builder, play_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_stop(self) -> None:
        builder = flatbuffers.Builder(128)
        Stop.Start(builder)
        stop_off = Stop.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.Stop)
        Message.AddCommand(builder, stop_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_play_clip(self, track_idx: int, slot_idx: int) -> None:
        builder = flatbuffers.Builder(128)
        PlayClip.Start(builder)
        PlayClip.AddTrackIndex(builder, track_idx)
        PlayClip.AddSlotIndex(builder, slot_idx)
        pc_off = PlayClip.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.PlayClip)
        Message.AddCommand(builder, pc_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_stop_track(self, track_idx: int) -> None:
        builder = flatbuffers.Builder(128)
        StopTrack.Start(builder)
        StopTrack.AddTrackIndex(builder, track_idx)
        st_off = StopTrack.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.StopTrack)
        Message.AddCommand(builder, st_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)

    def send_quit(self) -> None:
        builder = flatbuffers.Builder(128)
        Quit.Start(builder)
        quit_off = Quit.End(builder)
        
        Message.Start(builder)
        Message.AddCommandType(builder, Command.Command.Quit)
        Message.AddCommand(builder, quit_off)
        msg_off = Message.End(builder)
        builder.Finish(msg_off)
        self._send_flatbuffer(builder)


    def stop_backend_process(self) -> None:
        if self.backend:
            self.send_quit()
            try:
                self.backend.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self.backend.kill()
            self.backend = None

    def create_layout(self) -> None:
        # 1. Top Bar (Control Bar)
        self.build_top_bar()

        # 2. Bottom Bar (Status Bar) - Packed before main so it stays glued to bottom
        self.build_bottom_bar()

        # 3. Main Workspace (Browser + Session/Detail)
        self.main_paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        self.main_paned.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)

        # Left Panel: Browser
        self.browser_frame = tk.Frame(self.main_paned, bg=self.colors["bg_light"], width=200)
        self.main_paned.add(self.browser_frame, weight=1)
        self.build_browser(self.browser_frame)

        # Right Panel: Workspace Split
        self.workspace_paned = ttk.PanedWindow(self.main_paned, orient=tk.VERTICAL)
        self.main_paned.add(self.workspace_paned, weight=5)

        # Top Workspace: Session View
        self.session_frame = tk.Frame(self.workspace_paned, bg=self.colors["bg_mid"])
        self.workspace_paned.add(self.session_frame, weight=3)
        self.build_session_view(self.session_frame)

        # Bottom Workspace: Detail View
        self.detail_frame = tk.Frame(self.workspace_paned, bg=self.colors["bg_dark"])
        self.workspace_paned.add(self.detail_frame, weight=1)
        self.build_detail_view(self.detail_frame)

    def build_top_bar(self) -> None:
        top_frame = tk.Frame(self, bg=self.colors["bg_dark"], height=40, bd=1, relief=tk.RAISED)
        top_frame.pack(side=tk.TOP, fill=tk.X)

        # --- Song Info Section (Left) ---
        song_info_frame = tk.Frame(top_frame, bg=self.colors["bg_dark"])
        song_info_frame.pack(side=tk.LEFT, padx=10, pady=5)

        bpm_lbl = tk.Label(song_info_frame, text="120.00", bg=self.colors["bg_display"], font=("Courier", 10, "bold"), width=6, bd=1, relief=tk.SUNKEN)
        bpm_lbl.pack(side=tk.LEFT, padx=2)
        self.add_hover_hint(bpm_lbl, "Tempo: Global song tempo in Beats Per Minute (BPM).")

        time_sig_lbl = tk.Label(song_info_frame, text="4 / 4", bg=self.colors["bg_display"], font=("Courier", 10, "bold"), width=5, bd=1, relief=tk.SUNKEN)
        time_sig_lbl.pack(side=tk.LEFT, padx=2)
        self.add_hover_hint(time_sig_lbl, "Time Signature: Global time signature (Numerator / Denominator).")

        # --- Playback Section (Center-Left) ---
        playback_frame = tk.Frame(top_frame, bg=self.colors["bg_dark"])
        playback_frame.pack(side=tk.LEFT, padx=20, pady=5)

        self.play_btn = tk.Button(playback_frame, text="▶", bg=self.colors["bg_light"], activebackground=self.colors["btn_play"], width=3, command=self.send_play)
        self.play_btn.pack(side=tk.LEFT, padx=1)
        self.add_hover_hint(self.play_btn, "Play: Start playback from the current marker.")

        self.stop_btn = tk.Button(playback_frame, text="■", bg=self.colors["bg_light"], width=3, command=self.send_stop)
        self.stop_btn.pack(side=tk.LEFT, padx=1)
        self.add_hover_hint(self.stop_btn, "Stop: Stop playback. Click again to return to start.")

        rec_btn = tk.Button(playback_frame, text="●", bg=self.colors["bg_light"], activebackground=self.colors["btn_arm"], width=3)
        rec_btn.pack(side=tk.LEFT, padx=1)
        self.add_hover_hint(rec_btn, "Arrangement Record: Record session clips and automation into the Arrangement.")

        timestamp_lbl = tk.Label(playback_frame, text="1. 1. 1", bg=self.colors["bg_display"], font=("Courier", 10, "bold"), width=8, bd=1, relief=tk.SUNKEN)
        timestamp_lbl.pack(side=tk.LEFT, padx=10)
        self.add_hover_hint(timestamp_lbl, "Position: Current playback position (Bars . Beats . Sixteenths).")

        # --- Device Info Section (Right) ---
        device_frame = tk.Frame(top_frame, bg=self.colors["bg_dark"])
        device_frame.pack(side=tk.RIGHT, padx=10, pady=5)

        sample_rate_lbl = tk.Label(device_frame, text="44100 Hz", bg=self.colors["bg_dark"], fg=self.colors["text_light"], font=("Arial", 8))
        sample_rate_lbl.pack(side=tk.LEFT, padx=5)
        self.add_hover_hint(sample_rate_lbl, "Audio Engine: Current hardware sample rate.")

        cpu_lbl = tk.Label(device_frame, text="CPU: 3%", bg=self.colors["bg_display"], font=("Courier", 9), width=8, bd=1, relief=tk.SUNKEN)
        cpu_lbl.pack(side=tk.LEFT, padx=2)
        self.add_hover_hint(cpu_lbl, "CPU Load Meter: Current processing load. High values may cause audio dropouts.")


    def build_bottom_bar(self) -> None:
        bottom_frame = tk.Frame(self, bg=self.colors["bg_light"], bd=1, relief=tk.SUNKEN)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X)

        self.status_label = tk.Label(bottom_frame, text="Ready.", bg=self.colors["bg_light"], font=("Arial", 8))
        self.status_label.pack(side=tk.LEFT, padx=5, pady=2)

    def add_hover_hint(self, widget: tk.Widget, text: str) -> None:
        """Helper method to bind mouse hover events to the status bar."""
        def on_enter(event: tk.Event[Any], t: str = text) -> None:
            self.status_label.config(text=t)
        def on_leave(event: tk.Event[Any]) -> None:
            self.status_label.config(text="Ready.")

        widget.bind("<Enter>", on_enter)
        widget.bind("<Leave>", on_leave)

    def build_browser(self, parent: tk.Frame) -> None:
        header = tk.Label(parent, text="Browser", bg=self.colors["bg_dark"], fg=self.colors["text_light"])
        header.pack(fill=tk.X)

        self.browser_tree = ttk.Treeview(parent, show="tree")
        self.browser_tree.pack(fill=tk.BOTH, expand=True)
        self.add_hover_hint(self.browser_tree, "File Browser: Double-click a plugin or MIDI file to load.")

        self.vst_node = self.browser_tree.insert("", "end", text="Plugins", open=True)
        self.midi_node = self.browser_tree.insert("", "end", text="MIDI Files", open=True)

        self.populate_browser()
        self.browser_tree.bind("<Double-1>", self.on_browser_double_click)

    def populate_browser(self) -> None:
        # Scan VST3
        vst_paths: List[str] = ["testdata", os.path.expanduser("~/.vst3"), "/usr/lib/vst3", "/usr/local/lib/vst3"]
        for p in vst_paths:
            if os.path.exists(p):
                self.add_to_tree(self.vst_node, p, [".vst3"], "vst")

        # Scan MIDI
        midi_paths: List[str] = ["testdata", "."]
        for p in midi_paths:
            if os.path.exists(p):
                # Filter out some noise for recursion
                self.add_to_tree(self.midi_node, p, [".mid", ".midi"], "midi", exclude=[".git", "bazel-", "third_party"])

    def add_to_tree(self, parent_node: str, path: str, extensions: List[str], type_label: str, exclude: Optional[List[str]] = None) -> None:
        try:
            entries: List[str] = sorted(os.listdir(path))
        except OSError:
            return

        for entry in entries:
            if exclude and any(entry.startswith(ex) for ex in exclude):
                continue

            full_path: str = os.path.join(path, entry)

            # Check if this is a target item (e.g. .vst3 bundle or .mid file)
            is_target: bool = any(entry.lower().endswith(ext) for ext in extensions)

            if is_target:
                if type_label == "vst":
                    # For VST3, try to list internal plugins
                    plugins: List[Tuple[str, str]] = self.get_vst_plugins(full_path)
                    if len(plugins) > 1:
                        bundle_node: str = self.browser_tree.insert(parent_node, "end", text=entry, values=(full_path, "folder"))
                        for idx, name in plugins:
                            self.browser_tree.insert(bundle_node, "end", text=name, values=(full_path, "vst", idx))
                    elif len(plugins) == 1:
                        self.browser_tree.insert(parent_node, "end", text=plugins[0][1], values=(full_path, "vst", plugins[0][0]))
                    else:
                        self.browser_tree.insert(parent_node, "end", text=entry, values=(full_path, "vst", 0))
                else:
                    self.browser_tree.insert(parent_node, "end", text=entry, values=(full_path, type_label))
            elif os.path.isdir(full_path):
                # Add as folder node and recurse
                folder_node: str = self.browser_tree.insert(parent_node, "end", text=entry, open=False, values=("", "folder"))
                self.add_to_tree(folder_node, full_path, extensions, type_label, exclude)

    def get_vst_plugins(self, vst_path: str) -> List[Tuple[str, str]]:
        backend_bin: str = "./bazel-bin/hbk-play"
        if "PYTHON_RUNFILES" in os.environ:
            from runfiles import runfiles
            r = runfiles.Create()
            if r:
                res = r.Rlocation("hibiki/hbk-play")
                if res:
                    backend_bin = res

        if not os.path.exists(backend_bin):
            return []

        try:
            result: subprocess.CompletedProcess[str] = subprocess.run([backend_bin, "--list", vst_path], capture_output=True, text=True, timeout=2)
            if result.returncode != 0: return []

            plugins: List[Tuple[str, str]] = []
            if result.stdout:
                for line in result.stdout.splitlines():
                    if ":" in line:
                        idx, name = line.split(":", 1)
                        plugins.append((idx, name))
            return plugins
        except:
            return []

    def on_browser_double_click(self, event: tk.Event[ttk.Treeview]) -> None:
        selection = self.browser_tree.selection()
        if not selection: return
        item: str = selection[0]
        values: Any = self.browser_tree.item(item, "values")
        if not values: return

        path: str = values[0]
        file_type: str = values[1]
        if file_type == "folder": return

        if file_type == "vst":
            index: int = int(values[2]) if len(values) > 2 else 0
            self.send_load_plugin(self.selected_track, path, index)
            self.status_label.config(text=f"Loading {os.path.basename(path)} into Track {self.selected_track}")

            # Update track header name
            header_label: Optional[tk.Label] = self.track_headers.get(self.selected_track)
            if header_label:
                # If it's a sub-plugin, values[2] exists and the name in the tree is better
                instrument_name: str = self.browser_tree.item(item, "text")
                header_label.config(text=instrument_name)
        elif file_type == "midi":

            self.send_load_clip(self.selected_track, self.selected_slot, path)
            self.status_label.config(text=f"Loading {os.path.basename(path)} into Track {self.selected_track} Slot {self.selected_slot}")
            # Update clip button text visually
            btn: Optional[tk.Button] = self.clip_buttons.get((self.selected_track, self.selected_slot))
            if btn:
                btn.config(text="► " + os.path.basename(path)[:10])


    def build_session_view(self, parent: tk.Frame) -> None:
        header = tk.Label(parent, text="Session View", bg=self.colors["bg_dark"], fg=self.colors["text_light"])
        header.pack(fill=tk.X)

        tracks_container = tk.Frame(parent, bg=self.colors["bg_mid"])
        tracks_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        for i in range(1, 5):
            self.create_track(tracks_container, "empty", i)


        self.create_master_track(tracks_container)

    def create_track(self, parent: tk.Frame, name: str, idx: int) -> None:
        track_frame = tk.Frame(parent, bg=self.colors["bg_track"], bd=1, relief=tk.SUNKEN, width=80)
        track_frame.pack(side=tk.LEFT, fill=tk.Y, padx=2)
        self.track_frames[idx] = track_frame

        # Track Header Frame (Edit Button + Name)
        header_container = tk.Frame(track_frame, bg=self.colors["bg_dark"])
        header_container.pack(fill=tk.X)

        edit_btn = tk.Button(header_container, text="⚙", bg=self.colors["bg_dark"], fg=self.colors["text_light"],
                            relief=tk.FLAT, width=2, command=lambda: self.send_show_plugin_gui(idx, 0))
        edit_btn.pack(side=tk.LEFT)
        self.add_hover_hint(edit_btn, f"Plugin Editor: Click to open the custom GUI for the plugin on track {idx}.")

        track_header = tk.Label(header_container, text=name, bg=self.colors["bg_dark"], fg=self.colors["text_light"], height=2, wraplength=50)
        track_header.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.track_headers[idx] = track_header

        track_header.bind("<Button-1>", lambda e: self.select_track(idx))
        header_container.bind("<Button-1>", lambda e: self.select_track(idx))
        self.add_hover_hint(track_header, f"Track Name: Click to select the track '{name}'.")

        for j in range(5):
            btn = tk.Button(track_frame, text="", bg=self.colors["bg_light"], height=1, relief=tk.FLAT)
            btn.pack(fill=tk.X, padx=2, pady=1)
            def click_handler(t: int = idx, s: int = j) -> None:
                self.on_clip_click(t, s)
            btn.config(command=click_handler)
            self.clip_buttons[(idx, j)] = btn
            self.add_hover_hint(btn, f"Clip Slot {j+1}: Click to select and play.")

        tk.Frame(track_frame, bg=self.colors["bg_track"]).pack(fill=tk.Y, expand=True)

        # Panning
        pan_scale = tk.Scale(track_frame, from_=-50, to=50, orient=tk.HORIZONTAL, showvalue=False, length=60, sliderlength=10)
        pan_scale.pack()
        self.add_hover_hint(pan_scale, "Track Panning: Adjust the stereo panorama of this track.")

        # Volume
        vol_scale = tk.Scale(track_frame, from_=6, to=-70, orient=tk.VERTICAL, showvalue=False, length=120, sliderlength=15)
        vol_scale.pack()
        self.add_hover_hint(vol_scale, "Track Volume: Adjust the output volume of this track.")

        # Buttons
        btn_container = tk.Frame(track_frame, bg=self.colors["bg_track"])
        btn_container.pack(fill=tk.X, pady=4)

        btn_active = tk.Button(btn_container, text="1", bg=self.colors["btn_active"], font=("Arial", 7, "bold"))
        btn_active.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        btn_active.config(command=lambda: self.send_stop_track(idx))
        self.add_hover_hint(btn_active, f"Track Activator: Click to stop all clips on track {idx}.")

        btn_solo = tk.Button(btn_container, text="S", bg=self.colors["bg_light"], font=("Arial", 7, "bold"))
        btn_solo.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        self.add_hover_hint(btn_solo, "Solo: Mute all other tracks except soloed tracks.")

        btn_arm = tk.Button(btn_container, text="O", bg=self.colors["bg_light"], font=("Arial", 7, "bold"))
        btn_arm.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        self.add_hover_hint(btn_arm, "Arm Recording: Prepare this track to receive incoming audio/MIDI.")

    def select_track(self, idx: int) -> None:
        # Reset previous selection colors
        for f in self.track_frames.values():
            f.config(bg=self.colors["bg_track"])

        self.selected_track = idx
        self.track_frames[idx].config(bg=self.colors["btn_active"])
        self.status_label.config(text=f"Track {idx} selected.")
        self.refresh_detail_view()

    def on_clip_click(self, track_idx: int, slot_idx: int) -> None:
        # Select track and slot
        self.select_track(track_idx)

        # Reset previous slot highlights in this track
        for j in range(5):
            btn = self.clip_buttons.get((track_idx, j))
            if btn: btn.config(relief=tk.FLAT, bd=1)

        self.selected_slot = slot_idx
        btn = self.clip_buttons.get((track_idx, slot_idx))
        if btn: btn.config(relief=tk.SUNKEN, bd=2)

        self.send_play_clip(track_idx, slot_idx)
        self.status_label.config(text=f"Playing Clip {slot_idx} on Track {track_idx}")

    def create_master_track(self, parent: tk.Frame) -> None:
        master_frame = tk.Frame(parent, bg=self.colors["bg_mid"], bd=1, relief=tk.SUNKEN, width=100)
        master_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=2)

        tk.Label(master_frame, text="Master", bg="#404040", fg=self.colors["text_light"]).pack(fill=tk.X)

        for j in range(5):
            scene_btn = tk.Button(master_frame, text=f"{j+1}  ►", bg=self.colors["bg_light"], height=1, anchor="e")
            scene_btn.pack(fill=tk.X, padx=2, pady=1)
            self.add_hover_hint(scene_btn, f"Scene Launch: Launch all clips in row {j+1} simultaneously.")

        tk.Frame(master_frame, bg=self.colors["bg_mid"]).pack(fill=tk.Y, expand=True)

        tk.Label(master_frame, text="Master Vol", bg=self.colors["bg_mid"], font=("Arial", 7)).pack()
        master_vol = tk.Scale(master_frame, from_=6, to=-70, orient=tk.VERTICAL, showvalue=False, length=120, sliderlength=15)
        master_vol.pack(pady=5)
        self.add_hover_hint(master_vol, "Master Volume: Adjust the final output volume of the set.")

    def build_detail_view(self, parent: tk.Frame) -> None:
        self.detail_header = tk.Label(parent, text="Track Detail View (Instrument / Audio Effects)", bg="#404040", fg=self.colors["text_light"])
        self.detail_header.pack(fill=tk.X)

        self.devices_container = tk.Frame(parent, bg=self.colors["bg_light"])
        self.devices_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.refresh_detail_view()

    def refresh_detail_view(self) -> None:
        # Clear existing devices
        for widget in self.devices_container.winfo_children():
            widget.destroy()

        self.detail_header.config(text=f"Track {self.selected_track} Detail View")

        plugins = self.track_plugins.get(self.selected_track, [])
        if not plugins:
            empty_label = tk.Label(self.devices_container, text="No plugins loaded on this track. Double-click a VST in the browser to add one.", 
                                  bg=self.colors["bg_light"], fg=self.colors["text_dark"])
            empty_label.pack(pady=20)
            return

        for idx, plugin in enumerate(plugins):
            self.create_plugin_device(self.devices_container, plugin["name"], idx)

    def create_plugin_device(self, parent: tk.Frame, name: str, plugin_idx: int) -> None:
        device_frame = tk.Frame(parent, bg=self.colors["bg_mid"], bd=2, relief=tk.RAISED)
        device_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5)

        header = tk.Frame(device_frame, bg=self.colors["bg_dark"])
        header.pack(fill=tk.X)

        on_off = tk.Button(header, text="O", bg=self.colors["btn_active"], font=("Arial", 6), width=2)
        on_off.pack(side=tk.LEFT, padx=2)

        title = tk.Button(header, text=name, bg=self.colors["bg_dark"], fg=self.colors["text_light"], 
                          font=("Arial", 9, "bold"), relief=tk.FLAT,
                          command=lambda: self.send_show_plugin_gui(self.selected_track, plugin_idx))
        title.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.add_hover_hint(title, f"{name}: Click to open the custom plugin GUI.")

        params_container = tk.Frame(device_frame, bg=self.colors["bg_mid"])
        params_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # For now, we don't have the real list of parameters from the backend ACK yet.
        # We can add a few generic macro knobs that map to param 0, 1, 2...
        # or wait for a proper parameter query IPC.
        # Let's show 4 generic macro knobs as a placeholder that work if the plugin has them.
        for i in range(4):
            p_frame = tk.Frame(params_container, bg=self.colors["bg_mid"])
            p_frame.pack(side=tk.LEFT, padx=5)

            # Use a slightly larger scale for easier control
            scale = tk.Scale(p_frame, from_=1.0, to=0.0, resolution=0.01, orient=tk.VERTICAL, 
                             showvalue=False, length=80, sliderlength=15, bg=self.colors["bg_mid"])
            scale.set(0.5) # Default middle
            scale.pack()
            
            def make_cmd(p_idx=plugin_idx, p_id=i):
                return lambda val: self.send_set_param_value(self.selected_track, p_idx, p_id, float(val))
            
            scale.config(command=make_cmd())
            
            tk.Label(p_frame, text=f"Macro {i+1}", bg=self.colors["bg_mid"], font=("Arial", 7)).pack()

    def create_device(self, parent: tk.Frame, name: str, parameters: List[str]) -> None:
        # Keep old method for now or delete if unused
        pass

if __name__ == "__main__":
    app = Gui()
    app.mainloop()
