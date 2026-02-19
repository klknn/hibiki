import tkinter as tk
from tkinter import ttk
import subprocess
import os
import signal
import atexit

class Gui(tk.Tk):
    def __init__(self):
        super().__init__()
        
        self.title("hibiki")
        self.geometry("1024x768")
        
        # Classic Ableton utilitarian color palette
        self.colors = {
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
        self.create_layout()
        
        # Backend process management
        self.backend = None
        self.start_backend()
        atexit.register(self.stop_backend_process)

    def start_backend(self, vst_path=None, midi_path=None, plugin_index=0):
        # Default paths for manual execution
        if not vst_path: vst_path = "testdata/Dexed.vst3"
        if not midi_path: midi_path = "testdata/test.mid"
        backend_bin = "./bazel-bin/hbk-play"
        
        # Check if we are running under Bazel
        if "PYTHON_RUNFILES" in os.environ:
            from runfiles import runfiles
            r = runfiles.Create()
            backend_bin = r.Rlocation("hibiki/hbk-play")
            # If default paths are used, resolve them via runfiles
            if vst_path == "testdata/Dexed.vst3":
                vst_path = r.Rlocation("hibiki/testdata/Dexed.vst3")
            if midi_path == "testdata/test.mid":
                midi_path = r.Rlocation("hibiki/testdata/test.mid")

        if not os.path.exists(backend_bin):
            self.status_label.config(text=f"Error: {backend_bin} not found. Build it first.")
            return

        self.stop_backend_process()

        try:
            cmd = [backend_bin, vst_path, midi_path, str(plugin_index)]
            self.backend = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1
            )
            self.current_vst = vst_path
            self.current_midi = midi_path
            self.current_plugin_index = plugin_index
            self.status_label.config(text=f"Backend: {os.path.basename(vst_path)}[{plugin_index}] + {os.path.basename(midi_path)}")
        except Exception as e:
            self.status_label.config(text=f"Failed to start backend: {e}")




    def send_command(self, cmd):
        if self.backend and self.backend.poll() is None:
            try:
                self.backend.stdin.write(f"{cmd}\n")
                self.backend.stdin.flush()
                # We could read ACK here if we wanted to be more robust
            except Exception as e:
                self.status_label.config(text=f"Command error: {e}")
        else:
            self.status_label.config(text="Backend not running.")

    def stop_backend_process(self):
        if self.backend:
            self.send_command("QUIT")
            try:
                self.backend.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self.backend.kill()
            self.backend = None

    def create_layout(self):
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

    def build_top_bar(self):
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
        
        self.play_btn = tk.Button(playback_frame, text="▶", bg=self.colors["bg_light"], activebackground=self.colors["btn_play"], width=3, command=lambda: self.send_command("PLAY"))
        self.play_btn.pack(side=tk.LEFT, padx=1)
        self.add_hover_hint(self.play_btn, "Play: Start playback from the current marker.")
        
        self.stop_btn = tk.Button(playback_frame, text="■", bg=self.colors["bg_light"], width=3, command=lambda: self.send_command("STOP"))
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


    def build_bottom_bar(self):
        bottom_frame = tk.Frame(self, bg=self.colors["bg_light"], bd=1, relief=tk.SUNKEN)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X)
        
        self.status_label = tk.Label(bottom_frame, text="Ready.", bg=self.colors["bg_light"], font=("Arial", 8))
        self.status_label.pack(side=tk.LEFT, padx=5, pady=2)

    def add_hover_hint(self, widget, text):
        """Helper method to bind mouse hover events to the status bar."""
        # Using default arguments in lambda to capture the current value of 'text'
        widget.bind("<Enter>", lambda e, t=text: self.status_label.config(text=t))
        widget.bind("<Leave>", lambda e: self.status_label.config(text="Ready."))

    def build_browser(self, parent):
        header = tk.Label(parent, text="Browser", bg=self.colors["bg_dark"], fg=self.colors["text_light"])
        header.pack(fill=tk.X)
        
        self.browser_tree = ttk.Treeview(parent, show="tree")
        self.browser_tree.pack(fill=tk.BOTH, expand=True)
        self.add_hover_hint(self.browser_tree, "File Browser: Double-click a plugin or MIDI file to load.")
        
        self.vst_node = self.browser_tree.insert("", "end", text="Plugins", open=True)
        self.midi_node = self.browser_tree.insert("", "end", text="MIDI Files", open=True)

        self.populate_browser()
        self.browser_tree.bind("<Double-1>", self.on_browser_double_click)

    def populate_browser(self):
        # Scan VST3
        vst_paths = ["testdata", os.path.expanduser("~/.vst3"), "/usr/lib/vst3", "/usr/local/lib/vst3"]
        for p in vst_paths:
            if os.path.exists(p):
                self.add_to_tree(self.vst_node, p, [".vst3"], "vst")

        # Scan MIDI
        midi_paths = ["testdata", "."]
        for p in midi_paths:
            if os.path.exists(p):
                # Filter out some noise for recursion
                self.add_to_tree(self.midi_node, p, [".mid", ".midi"], "midi", exclude=[".git", "bazel-", "third_party"])

    def add_to_tree(self, parent_node, path, extensions, type_label, exclude=None):
        try:
            entries = sorted(os.listdir(path))
        except OSError:
            return

        for entry in entries:
            if exclude and any(entry.startswith(ex) for ex in exclude):
                continue

            full_path = os.path.join(path, entry)
            
            # Check if this is a target item (e.g. .vst3 bundle or .mid file)
            is_target = any(entry.lower().endswith(ext) for ext in extensions)
            
            if is_target:
                if type_label == "vst":
                    # For VST3, try to list internal plugins
                    plugins = self.get_vst_plugins(full_path)
                    if len(plugins) > 1:
                        bundle_node = self.browser_tree.insert(parent_node, "end", text=entry, values=(full_path, "folder"))
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
                folder_node = self.browser_tree.insert(parent_node, "end", text=entry, open=False, values=("", "folder"))
                self.add_to_tree(folder_node, full_path, extensions, type_label, exclude)

    def get_vst_plugins(self, vst_path):
        backend_bin = "./bazel-bin/hbk-play"
        if "PYTHON_RUNFILES" in os.environ:
            from runfiles import runfiles
            r = runfiles.Create()
            backend_bin = r.Rlocation("hibiki/hbk-play")
        
        if not os.path.exists(backend_bin):
            return []

        try:
            result = subprocess.run([backend_bin, "--list", vst_path], capture_output=True, text=True, timeout=2)
            if result.returncode != 0: return []
            
            plugins = []
            for line in result.stdout.splitlines():
                if ":" in line:
                    idx, name = line.split(":", 1)
                    plugins.append((idx, name))
            return plugins
        except:
            return []

    def on_browser_double_click(self, event):
        item = self.browser_tree.selection()[0]
        values = self.browser_tree.item(item, "values")
        if not values: return

        path, file_type = values[0], values[1]
        if file_type == "folder": return
        
        if file_type == "vst":
            index = values[2] if len(values) > 2 else 0
            self.start_backend(vst_path=path, midi_path=self.current_midi, plugin_index=index)
        elif file_type == "midi":
            self.start_backend(vst_path=self.current_vst, midi_path=path, plugin_index=self.current_plugin_index)




    def build_session_view(self, parent):
        header = tk.Label(parent, text="Session View", bg=self.colors["bg_dark"], fg=self.colors["text_light"])
        header.pack(fill=tk.X)
        
        tracks_container = tk.Frame(parent, bg=self.colors["bg_mid"])
        tracks_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        for i in range(1, 5):
            self.create_track(tracks_container, f"{i} Audio", i)
            
        self.create_master_track(tracks_container)

    def create_track(self, parent, name, idx):
        track_frame = tk.Frame(parent, bg=self.colors["bg_track"], bd=1, relief=tk.SUNKEN, width=80)
        track_frame.pack(side=tk.LEFT, fill=tk.Y, padx=2)
        
        track_header = tk.Label(track_frame, text=name, bg=self.colors["bg_dark"], fg=self.colors["text_light"])
        track_header.pack(fill=tk.X)
        self.add_hover_hint(track_header, f"Track Name: Click to select the track '{name}'.")
        
        for j in range(5):
            clip_color = self.colors["bg_light"] if j != idx else self.colors["btn_active"]
            clip_text = "►  Clip" if j == idx else ""
            btn = tk.Button(track_frame, text=clip_text, bg=clip_color, height=1, relief=tk.FLAT)
            btn.pack(fill=tk.X, padx=2, pady=1)
            self.add_hover_hint(btn, "Clip Slot: Double-click to create an empty clip, or click ► to launch.")
            
        tk.Frame(track_frame, bg=self.colors["bg_track"]).pack(fill=tk.Y, expand=True)
        
        # Panning
        pan_scale = tk.Scale(track_frame, from_=-50, to=50, orient=tk.HORIZONTAL, showvalue=0, length=60, sliderlength=10)
        pan_scale.pack()
        self.add_hover_hint(pan_scale, "Track Panning: Adjust the stereo panorama of this track.")
        
        # Volume
        vol_scale = tk.Scale(track_frame, from_=6, to=-70, orient=tk.VERTICAL, showvalue=0, length=120, sliderlength=15)
        vol_scale.pack()
        self.add_hover_hint(vol_scale, "Track Volume: Adjust the output volume of this track.")
        
        # Buttons
        btn_container = tk.Frame(track_frame, bg=self.colors["bg_track"])
        btn_container.pack(fill=tk.X, pady=4)
        
        btn_active = tk.Button(btn_container, text="1", bg=self.colors["btn_active"], font=("Arial", 7, "bold"))
        btn_active.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        self.add_hover_hint(btn_active, "Track Activator: Mute or unmute this track.")
        
        btn_solo = tk.Button(btn_container, text="S", bg=self.colors["bg_light"], font=("Arial", 7, "bold"))
        btn_solo.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        self.add_hover_hint(btn_solo, "Solo: Mute all other tracks except soloed tracks.")
        
        btn_arm = tk.Button(btn_container, text="O", bg=self.colors["bg_light"], font=("Arial", 7, "bold"))
        btn_arm.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        self.add_hover_hint(btn_arm, "Arm Recording: Prepare this track to receive incoming audio/MIDI.")

    def create_master_track(self, parent):
        master_frame = tk.Frame(parent, bg=self.colors["bg_mid"], bd=1, relief=tk.SUNKEN, width=100)
        master_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=2)
        
        tk.Label(master_frame, text="Master", bg="#404040", fg=self.colors["text_light"]).pack(fill=tk.X)
        
        for j in range(5):
            scene_btn = tk.Button(master_frame, text=f"{j+1}  ►", bg=self.colors["bg_light"], height=1, anchor="e")
            scene_btn.pack(fill=tk.X, padx=2, pady=1)
            self.add_hover_hint(scene_btn, f"Scene Launch: Launch all clips in row {j+1} simultaneously.")
            
        tk.Frame(master_frame, bg=self.colors["bg_mid"]).pack(fill=tk.Y, expand=True)
        
        tk.Label(master_frame, text="Master Vol", bg=self.colors["bg_mid"], font=("Arial", 7)).pack()
        master_vol = tk.Scale(master_frame, from_=6, to=-70, orient=tk.VERTICAL, showvalue=0, length=120, sliderlength=15)
        master_vol.pack(pady=5)
        self.add_hover_hint(master_vol, "Master Volume: Adjust the final output volume of the set.")

    def build_detail_view(self, parent):
        header = tk.Label(parent, text="Track Detail View (Instrument / Audio Effects)", bg="#404040", fg=self.colors["text_light"])
        header.pack(fill=tk.X)
        
        devices_container = tk.Frame(parent, bg=self.colors["bg_light"])
        devices_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.create_device(devices_container, "Simpler", ["Start", "Loop", "Length", "Fade"])
        self.create_device(devices_container, "EQ Eight", ["Freq", "Res", "Gain"])
        self.create_device(devices_container, "Reverb", ["Decay", "Density", "Dry/Wet"])

    def create_device(self, parent, name, parameters):
        device_frame = tk.Frame(parent, bg=self.colors["bg_mid"], bd=2, relief=tk.RAISED)
        device_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5)
        
        header = tk.Frame(device_frame, bg=self.colors["bg_dark"])
        header.pack(fill=tk.X)
        
        on_off = tk.Button(header, text="O", bg=self.colors["btn_active"], font=("Arial", 6), width=2)
        on_off.pack(side=tk.LEFT, padx=2)
        self.add_hover_hint(on_off, f"Device Activator: Turn {name} on or off.")
        
        title = tk.Label(header, text=name, bg=self.colors["bg_dark"], fg=self.colors["text_light"], font=("Arial", 9, "bold"))
        title.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.add_hover_hint(title, f"{name}: Device header. Drag to reorder devices in the chain.")
        
        params_container = tk.Frame(device_frame, bg=self.colors["bg_mid"])
        params_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        for param in parameters:
            p_frame = tk.Frame(params_container, bg=self.colors["bg_mid"])
            p_frame.pack(side=tk.LEFT, padx=5)
            
            scale = tk.Scale(p_frame, from_=100, to=0, orient=tk.VERTICAL, showvalue=0, length=80, sliderlength=15)
            scale.pack()
            self.add_hover_hint(scale, f"Macro Control: Adjust the {param} parameter for {name}.")
            
            tk.Label(p_frame, text=param, bg=self.colors["bg_mid"], font=("Arial", 7)).pack()

if __name__ == "__main__":
    app = Gui()
    app.mainloop()