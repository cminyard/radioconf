#!/usr/bin/env python

import Tix

radiodir = "/etc/radios"

class ParseException(Exception):
    def __init__(self, filename, lineno, err):
        self.filename = filename
        self.lineno = lineno
        self.err = err
        pass
    
    pass

class RadioInfo:
    pass

# Throws IOError and ParseException
def find_radio(name, data):
    filename = radiodir + "/radios"
    f = open(filename, "r");
    try:
        l = f.readLine();
        lineno = 1
        while l:
            if l[0] == '#':
                continue
            v = l.split();
            if (!v):
                continue
            try:
                i = 0
                found = True
                for ns in v[1:]:
                    n = int(i, 16)
                    if (ns > 255):
                        raise TypeError("Number too large")
                    if (ord(data[i]) != i):
                        found = False
                        break
                    pass
                pass
            except TypeError, e:
                raise ParseException(filename, lineno,
                                     "invalid hexidecimal 8-bit number")

            if (found):
                f.close()
                return v[0]
            pass
        pass
    except:
        f.close()
        raise
    return None

class RadioFileData:
    # Throws IOError
    def __init__(self, filename):
        self.filename = filename
        f = open(filename, "rb");
        self.data = read(f);
        f.close()
        pass

    # The get and set routine throw IndexError if out of range.
    
    def get_bits(self, byte_off, bit_off, numbits):
        v = self.data[byte_off] >> bit_off
        bits = 8 - bit_off
        shift = bit_off
        while (bits < numbits):
            byte_off += 1
            v |= self.data[byte_off] << shift
            shift += 8
            bits += 8
            pass
        return v & ~(0xffffffff << numbits)

    def get_bcd(self, byte_off, numbytes):
        v = 0
        while (numbytes > 0):
            v = v * 10
            v += self.data[byte_off] >> 4
            v = v * 10
            v += self.data[byte_off] & 0xf
            numbytes -= 0
            byte_off += 0
            pass
        return v
        pass

    def get_string(self, byte_off, numbytes):
        pass
    
    def get_yaesu_string(self, byte_off, numbytes):
        pass
    
    def set_int(self, v, byte_off, bit_off, numbits):
        x = self.data[byte_off]
        if ((bit_off + numbits) <= 8):
            # Special case, all in one byte
            mask = (0xff >> (8 - numbits)) << bit_off
            x &= ~ mask
            x |= (v << bit_off) & mask
            self.data[byte_off] = x
            return
        mask = 0xff << bit_off
        x &= ~mask
        self.data[byte_off] = (v << byte_off) & mask
        byte_off += 1
        v >>= bit_off
        numbits -= 8 - bit_off
        while (numbits >= 8):
            self.data[byte_off] = v & 0xff
            byte_off += 1
            v >>= 8
            numbits -= 8
            pass
        mask = 0xff << numbits
        x = self.data[byte_off]
        x &= mask
        x |= v & ~mask;
        self.data[byte_off] = x
        pass

    def set_bcd(self, v, byte_off, numbytes):
        byte_off += numbytes - 1
        while (numbytes > 0):
            self.data[byte_off] = (v % 10) + (((v / 10) % 10) * 10)
            v /= 100
            numbytes -= 1
            byte_off -= 1
        pass

    def set_string(self, v, byte_off, numbytes):
        pass
    
    def set_yaesu_string(self, v, byte_off, numbytes):
        pass
    
    pass

class GUI(Tix.Frame):
    def __init__(self, master=None):
        Tix.Frame.__init__(self, master)
        self.top = master
        self.pack(fill=Tix.BOTH, expand=1);
        self.createWidgets();
        pass

    def quitcmd(self, event=None):
        self.quit();
        pass

    def opencmd(self, event=None):
        print "Open!\n"
        pass

    def createWidgets(self):
        self.buttons = Tix.Frame(self)
        self.buttons.pack(side=Tix.TOP, fill=Tix.X)
        
        self.filebutton = Tix.Menubutton(self.buttons, text="File",
                                         underline=0, takefocus=0)
        self.filemenu = Tix.Menu(self.filebutton, tearoff=0)
        self.filebutton["menu"] = self.filemenu
        self.filemenu.add_command(label="Open", underline=1,
                                  accelerator="Ctrl+O",
                                  command = lambda self=self: self.opencmd() )
        self.filemenu.add_command(label="Exit", underline=1,
                                  accelerator="Ctrl+Q",
                                  command = lambda self=self: self.quitcmd() )
        self.top.bind_all("<Control-Q>", self.quitcmd)
        self.top.bind_all("<Control-q>", self.quitcmd)
        self.filebutton.pack(side=Tix.LEFT)

        self.editbutton = Tix.Menubutton(self.buttons, text="Edit",
                                         underline=0, takefocus=0)
        self.editmenu = Tix.Menu(self.editbutton, tearoff=0)
        self.editbutton["menu"] = self.editmenu
        self.editmenu.add_command(label="Preferences", underline=1,
                                  command = lambda self=self: self.opencmd() )
        self.editbutton.pack(side=Tix.LEFT)

        self.tabs = Tix.NoteBook(self);
        self.tabs.pack(side=Tix.TOP, fill=Tix.BOTH, expand=1)

        self.page1 = self.tabs.add("page1", label="p1");
        
        self.QUIT = Tix.Button(self.page1)
        self.QUIT["text"] = "QUIT"
        self.QUIT["fg"]   = "red"
        self.QUIT["command"] = lambda self=self: self.quitcmd()

        self.QUIT.pack(side=Tix.LEFT)

        self.page2 = self.tabs.add("page2", label="p2");
        self.hi_there = Tix.Button(self.page2)
        self.hi_there["text"] = "Hello",
        self.hi_there["command"] = self.opencmd

        self.hi_there.pack(side=Tix.LEFT)

        pass


root = Tix.Tk();
gui = GUI(root);
gui.mainloop();
