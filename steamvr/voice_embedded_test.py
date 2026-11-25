"""Exercise hidden recognizer startup/control/shutdown without opening a microphone."""
import ctypes, json, mmap, os, struct, subprocess, sys, time, tempfile
from pathlib import Path

def main():
    exe = Path(sys.argv[1]).resolve()
    temporary = tempfile.TemporaryDirectory()
    pid = os.getpid()
    name = "Local\\D2RVRVoiceIntegrated" + str(pid)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    mutex = kernel.CreateMutexW(None, False, name + "Mutex")
    assert mutex
    with mmap.mmap(-1, 8192, tagname=name) as control:
        def change(generation, mode, stop=0):
            assert kernel.WaitForSingleObject(mutex, 1000) in (0, 128)
            try:
                data = json.dumps(dict(Storage=temporary.name, Slot=0, Sample=0, Tolerance=1, Microphone=-1,
                                       Bindings=[dict(Phrase="", Skill=149) for _ in range(6)])).encode()
                control[256:256+len(data)] = data
                struct.pack_into("4I", control, 0, generation, mode, len(data), stop)
            finally: kernel.ReleaseMutex(mutex)
        def status():
            assert kernel.WaitForSingleObject(mutex, 1000) in (0, 128)
            try: return control[16:256].split(b"\0",1)[0].decode()
            finally: kernel.ReleaseMutex(mutex)
        def expect(text):
            until = time.monotonic() + 12
            while time.monotonic() < until:
                assert child.poll() is None, "Recognizer exited unexpectedly"
                if text in status(): return
                time.sleep(.05)
            raise AssertionError("Expected " + text + "; status: " + status())
        change(1, 1)
        child = subprocess.Popen([str(exe), "--embedded", str(pid)], creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            expect("Record three examples")  # Missing samples fail BEFORE microphone construction.
            change(2, 0)
            expect("Recorded sound recognition off")
            change(3, 0, 1)
            assert child.wait(timeout=5) == 0
        finally:
            if child.poll() is None:
                change(4, 0, 1)
                child.wait(timeout=5)
    kernel.CloseHandle(mutex)
    temporary.cleanup()
    print("Hidden recognition control, in-game error feedback, Off and shutdown passed; no microphone opened")
if __name__ == "__main__": main()
