"""QEMU interaction checks for the direct-manipulation OS Explorer.
Old course-section/badge expectations are intentionally replaced by the new UX;
original terminal/live/process/application/persistence assertions still run.
"""
import re
import time
from test_storage import ROOT, require


def check_course(command, screenshot, pixel, key):
    font=(ROOT/"kernel/font.c").read_text().split("static const uint8_t font[95][5] = {",1)[1].split("};",1)[0]
    glyphs={tuple(int(n.strip(),0) for n in row.split(",")):chr(ch)
            for ch,row in enumerate(re.findall(r"\{([^{}]+)\}",font),32)}
    def read(frame,x,y,count,scale=1):
        result=""
        for cell in range(count):
            bits=[]
            for col in range(5):
                mask=0
                for row in range(7):
                    if pixel(frame,x+(cell*6+col)*scale,y+row*scale) in (
                        bytes.fromhex("203449"),bytes.fromhex("087e83"),bytes.fromhex("536878"),
                        bytes.fromhex("a96816"),bytes.fromhex("b6423a")):
                        mask|=1<<row
                bits.append(mask)
            result+=glyphs.get(tuple(bits),"?")
        return result.rstrip()
    def context(frame):
        return " ".join(read(frame,180,frame[1]-213+row*22,(frame[0]-204)//12,2) for row in range(5))
    def stable_frame(name):
        # Rendering is deferred and a screendump can capture a half-cleared frame.
        # Require two identical samples before comparing a paused UI's pixels.
        deadline = time.monotonic() + 8
        previous = None
        while True:
            frame = screenshot(name, settle=0.3)
            if previous is not None and frame[2] == previous[2]:
                return frame
            require(time.monotonic() < deadline, f"{name}: rendering did not settle")
            previous = frame
    def observe(name,expected):
        deadline=time.monotonic()+4
        while True:
            frame=stable_frame("explorer-"+name)
            if expected in context(frame):return frame
            require(time.monotonic()<deadline,f"{name}: expected contextual result {expected!r}, saw {context(frame)!r}")
    def keys(*codes):
        for code in codes:key(code)
    def open_next():keys("esc","down","ret")
    home=screenshot("explorer-home")
    require("Try a mechanism. Change it. See why." in read(home,24,66,40,2),"boot did not prioritize interactive explorers")
    require(b"System Timer Ticks:" in command("ticks"),"serial shell unavailable under explorer")
    # Every contextual panel must restore the actual rendered experiment exactly.
    help_names = ("Processes", "CPU scheduling", "Physical memory", "Virtual memory",
                  "Heap allocation", "Interrupts", "System calls", "Filesystems",
                  "Concurrency", "Keyboard input", "Framebuffer", "OS architecture",
                  "Boot dependencies", "CPU execution", "Failure investigation")
    for index, name in enumerate(help_names):
        keys("ret", "r", "spc")
        before = stable_frame(f"help-{index}-experiment")
        # Check the visible button independently against the bitmap font in white.
        label = "[H] How to use"
        reverse = {ch: bits for bits, ch in glyphs.items()}
        for cell, ch in enumerate(label):
            for col, bits in enumerate(reverse[ch]):
                for row in range(7):
                    expected = "ffffff" if bits & (1 << row) else "087e83"
                    require(pixel(before, before[0]-200+(cell*6+col)*2, 67+row*2) == bytes.fromhex(expected),
                            f"{name}: help button missing")
        key("h")
        panel = stable_frame(f"help-{index}-start")
        require(read(panel,180,116,20,2) == "Start here", f"{name}: help did not open")
        require(name in read(panel,180,146,50,2), f"{name}: wrong contextual instructions")
        key("pgdn")
        paged = stable_frame(f"help-{index}-scrolled")
        require(paged[2] != panel[2], f"{name}: help did not scroll")
        key("home")
        require(stable_frame(f"help-{index}-top")[2] == panel[2], f"{name}: help Home did not restore top")
        keys("r", "c", "1", "e", "i", "h")
        require(stable_frame(f"help-{index}-return")[2] == before[2], f"{name}: help changed experiment")
        keys("r", "esc", "down")
    print("QEMU context help PASS: visible button, relevant instructions, paging/Home, isolated controls and exact return for all 15 mechanisms")
    key("ret")
    initial=observe("process-initial","Select a process")
    require("Model pages:" in read(initial,180,116,20),"first entry did not open a process experiment directly")
    keys("c","c");observe("two-children","Created child PID 12")
    keys("3","x");zombie=observe("zombie","PID 11 exited status 7")
    require("ZOMBIE / terminated" in read(zombie,362,241,20,2),"terminated child not visibly marked zombie")
    keys("2","w");observe("reap","Collected exit status 7")
    keys("2","x");orphan=observe("orphan","Living children were adopted")
    require("ADOPTED / living orphan" in read(orphan,392,303,24),"living adopted child not distinct from zombie")
    keys("4","s");observe("dispatch","Dispatched the selected process")
    keys("b","u");observe("wake","Selected process is READY")
    keys("1","w","4","x","z");observe("child-reaped","The child's parent collected status 7")
    keys("r","c","w","3","x");observe("wait-first","waiting parent collected the status immediately")
    key("r");require(observe("process-reset","Select a process")[2]==initial[2],"process reset was not deterministic")
    keys("e","pgdn");deep=screenshot("explorer-explanation")
    key("esc");require(observe("process-return","Select a process")[2]==initial[2],"explanation changed process state")
    key("i");source=screenshot("explorer-source")
    require("SOURCE-VERIFIED" in read(source,180,116,35,2),"real implementation reference missing")
    key("esc")
    open_next()
    keys("g","q","n","4","a","spc","spc","b","spc","w")
    observe("scheduler-wake","Task awakened")
    key("x");time.sleep(0.6);key("x");observe("scheduler-paused","Animation paused")
    key("r");observe("scheduler-reset","Space runs one tick")
    open_next()
    frame_initial=observe("frames-initial","Thirteen usable frames")
    keys("a","a","left","f");observe("frame-free","Freed the selected frame")
    for _ in range(14):key("a")
    observe("frame-exhaustion","Allocation failed")
    key("r");require(observe("frames-reset","Thirteen usable frames")[2]==frame_initial[2],"frame reset changed initial bitmap")
    open_next()
    keys("2","spc","spc");observe("missing-mapping","STOP at PD[3]")
    keys("m","spc","spc","spc","spc");observe("mapped","Translation succeeded")
    keys("u","spc","spc");observe("unmapped","STOP at PD[3]")
    keys("3","w","spc","spc","spc");observe("permission-fault","STOP at PT write permissions")
    keys("p","spc","spc","spc","spc");observe("permission-restored","Translation succeeded")
    keys("4","spc");observe("noncanonical","STOP before PML4")
    open_next()
    keys("a","a","left","f","right","f");observe("heap-coalesced","Adjacent free neighbors coalesced")
    for _ in range(10):key("a")
    observe("heap-exhaustion","Allocation refused")
    key("r");observe("heap-reset","256-byte arena starts")
    open_next()
    keys("k","m","spc");observe("masked-irq","Entry delayed")
    keys("m","spc","spc","spc","spc","spc");observe("irq-return","iretq restored")
    open_next()
    keys("p","c","spc");observe("syscall-rejected","EFAULT (-2)")
    keys("spc","p","c","spc","spc","spc");observe("syscall-return","Returned to Ring 3")
    open_next()
    keys("3","d");observe("file-missing","Lookup failed")
    keys("2","d");observe("file-empty","EOF: zero bytes")
    keys("1","d");observe("file-read","copied 8 bytes")
    keys("o","o","o","o","d");observe("file-eof","EOF: zero bytes")
    open_next()
    keys("1","2","1","2","1","2")
    race=observe("lost-update","Task stored its local value")
    require(read(race,390,116,1,2)=="1","chosen race interleaving did not lose the update")
    keys("r","l","1","1","1","2","2","2")
    locked=observe("locked-update","Task stored its local value")
    require(read(locked,390,116,1,2)=="2","locked transactions failed to retain both updates")
    keys("d","l","1","2","1","2");observe("deadlock","DEADLOCK:")
    key("f");observe("deadlock-recovery","released its resources")
    open_next()
    for _ in range(9):key("k")
    observe("input-overflow","Input ring FULL")
    key("spc");observe("input-consumed","Consumer removed one A event")
    open_next()
    drawing=observe("pixels-initial","Move with arrows")
    keys("spc","right","down","f");painted=observe("pixels-painted","3x2 rectangle changed")
    require(painted[2]!=drawing[2],"drawing controls did not change pixels")
    key("r");require(observe("pixels-reset","Move with arrows")[2]==drawing[2],"pixel reset not deterministic")
    for n in range(4):
        open_next();before=screenshot(f"explorer-support-{n}")
        keys("spc","b");back=screenshot(f"explorer-support-back-{n}")
        key("r");reset=screenshot(f"explorer-support-reset-{n}")
        width,height,_=before
        start=116*width*3;end=(height-244)*width*3
        require(before[2][start:end]==back[2][start:end]==reset[2][start:end],
                "supporting trace back/reset did not restore its canvas")
    key("esc")  # Return to home before the unchanged pager navigation regression.
    key("tab");screenshot("explorer-tools")
    key("tab");command("run colors");key("tab")
    restored=screenshot("explorer-canvas-restored")
    for i,color in enumerate(("ff0000","00ff00","0000ff","00ffff","ff00ff","ffff00")):
        require(pixel(restored,restored[0]-192-24+i*32+16,20)==bytes.fromhex(color),"hidden application canvas was not preserved")
    print("EXPLORER PASS: direct entry, mutable process lifecycles, four-policy scheduling controls,")
    print("frames, map/unmap/permissions, heap fragmentation, IRQ/syscall/file experiments, races,")
    print("deadlock/recovery, input overflow, drawing/reset, source/explanation and preserved Tools")
