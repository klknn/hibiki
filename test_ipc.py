import struct
import subprocess
import time
import flatbuffers
from hibiki.ipc import Message, Command, Quit

def main():
    import os, sys
    print(f"DEBUG: sys.path = {sys.path}")
    print(f"DEBUG: os.environ.keys() = {list(os.environ.keys())}")

    backend_bin = "./bazel-bin/hbk-play"
    try:
        from bazel_tools.tools.python.runfiles import runfiles
        r = runfiles.Create()

        if r:
            res = r.Rlocation("_main/hbk-play")
            if not res:
                res = r.Rlocation("hibiki/hbk-play")
            
            if res:
                backend_bin = res
                print(f"DEBUG: Found backend_bin via runfiles: {backend_bin}")
    except Exception as e:
        print(f"DEBUG: Error initializing runfiles: {e}")
    
    print(f"DEBUG: Using backend_bin = {backend_bin}")

    process = subprocess.Popen(
        [backend_bin],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0
    )
    
    print("Backend started. Sending QUIT via FlatBuffer...")
    
    builder = flatbuffers.Builder(64)
    Quit.QuitStart(builder)
    quit_offset = Quit.QuitEnd(builder)
    
    Message.MessageStart(builder)
    Message.MessageAddCommandType(builder, Command.Command.Quit)
    Message.MessageAddCommand(builder, quit_offset)
    msg_offset = Message.MessageEnd(builder)
    builder.Finish(msg_offset)
    
    buf = builder.Output()
    
    # Send length-prefixed message
    process.stdin.write(struct.pack("<I", len(buf)))
    process.stdin.write(buf)
    process.stdin.flush()
    
    print("QUIT sent. Waiting for backend to exit...")
    
    try:
        stdout, stderr = process.communicate(timeout=5)
        print("Backend exited.")
        print("STDOUT:", stdout.decode())
        print("STDERR:", stderr.decode())
    except subprocess.TimeoutExpired:
        print("Error: Backend did not exit in time.")
        process.kill()

if __name__ == "__main__":
    main()
