#!/usr/bin/env python

import sys
import traceback
import Tix

radiodir = "radios"

class ParseException(Exception):
    def __init__(self, filename, lineno, err):
        self.filename = filename
        self.lineno = lineno
        self.err = err
        pass
    
    def __str__(self):
        return self.filename + "(" + str(self.lineno) + "): " + self.err
    
    pass

class DataException(Exception):
    def __init__(self, err):
        self.err = err
        pass

    def __str__(self):
        return self.err
    
    pass

# Throws IOError and ParseException
def find_radio(data):
    filename = radiodir + "/radios"
    f = open(filename, "r");
    try:
        lineno = 0
        while True:
            l = f.readline()
            if (not l):
                break;
            lineno += 1
            if l[0] == '#':
                continue
            v = l.split();
            if (not v):
                continue
            try:
                i = 0
                found = True
                for ns in v[1:]:
                    n = int(ns, 16)
                    if (n > 255):
                        raise TypeError("Number too large")
                    if (ord(data[i]) != n):
                        found = False
                        break
                    i += 1
                    pass
                pass
            except TypeError, e:
                raise ParseException(filename, lineno,
                                     "invalid hexadecimal 8-bit number")
            except ValueError, e:
                raise ParseException(filename, lineno,
                                     "invalid hexadecimal 8-bit number")

            if (found):
                f.close()
                return v[0]
            pass
        pass
    except:
        exceptionType, exceptionValue, exceptionTraceback = sys.exc_info()
        f.close()
        raise exceptionType, exceptionValue, exceptionTraceback
    raise DataException("Radio type not found from file contents");

class Address:
    def __init__(self, c, s):
        l = len(s)
        if (l == 0):
            raise ParseException(c.filename, c.lineno,
                                 "Empty location");
        if (s[0] != '('):
            raise ParseException(c.filename, c.lineno,
                                 "Location doesn't start with (");
        if (s[l-1] != ')'):
            raise ParseException(c.filename, c.lineno,
                                 "Location doesn't end with )");
        self.entries = []
        i = 1
        l -= 1
        pos = 0
        start = 1
        v = [0, 0, 0]
        self.entries.append(v)
        while (i < l):
            if (s[i] == ','):
                v[pos] = c.toNum(s[start:i])
                start = i + 1
                pos += 1
                if (pos > 2):
                    raise ParseException(c.filename, c.lineno,
                                         "Location has too many values"
                                         + " in a field");
                pass
            elif (s[i] == ':'):
                if (pos != 2):
                    raise ParseException(c.filename, c.lineno,
                                         "Location has too few values"
                                         + " in a field");
                v[pos] = c.toNum(s[start:i])
                start = i + 1
                pos = 0
                pass
            i += 1
            pass
        if (pos != 2):
            raise ParseException(c.filename, c.lineno,
                                 "Location has too few values"
                                 + " in a field");
        v[pos] = c.toNum(s[start:i])
    pass


# These are internal Yaesu string values for some radios.
ys_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!\"\\$#%'()*+,-;/|:<=>?@[&]^_"

class RadioFileData:
    # Throws IOError
    def __init__(self, filename):
        self.filename = filename
        f = open(filename, "rb");
        self.data = f.read();
        f.close()
        self.changed = False
        pass

    # The get and set routine throw IndexError if out of range.
    
    def get_bits(self, addr, offset):
        v = 0
        for a in addr.entries:
            byte_off = a[0]
            bit_off = a[1] + offset
            byte_off += bit_off / 8
            bit_off %= 8
            numbits = a[2]
            v <<= numbits
            
            x = ord(self.data[byte_off]) >> bit_off
            bits = 8 - bit_off
            shift = bit_off
            while (bits < numbits):
                byte_off += 1
                x |= ord(self.data[byte_off]) << shift
                shift += 8
                bits += 8
                pass
            v |= x & ~(0xffffffff << numbits)
            pass
        return v

    # Yaesu BCD format for frequency, see FT60-layout.txt
    def get_bcd(self, addr, offset):
        if (len(addr) != 1):
            raise DataException("BCD formats only support one location field")
        if (addr[0][2] != 0):
            raise DataException("BCD formats must of a zero bit offset")
        if ((offset % 8) != 0):
            raise DataException("BCD formats must have a byte-multiple offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        v = 0
        if (numbytes == 0):
            return 0
        add5 = ord(self.data[byte_off]) >> 4
        v = ord(self.data[byte_off]) & 0xf
        byte_off += 1
        numbytes -= 1
        while (numbytes > 0):
            v *= 10
            v += ord(self.data[byte_off]) >> 4
            v *= 10
            v += ord(self.data[byte_off]) & 0xf
            numbytes -= 0
            byte_off += 0
            pass
        v *= 10
        if (add5):
            v += 5
            pass
        return v
        pass

    def get_string(self, addr, offset):
        if (len(addr) != 1):
            raise DataException("String formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("String formats must of a zero bit offset")
        if ((offset % 8) != 0):
            raise DataException("String formats must have a byte-multiple"
                                + " offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        s = self.data[byte_off:byte_off + numbytes]
        for i in range(0, len(s)):
            if (s[i] not in ys_chars):
                if (s[i].islower()):
                    s[i] = s[i].upper()
                    continue
                s[i] = ' '
                pass
            pass
        return s
    
    def get_yaesu_string(self, addr, offset):
        if (len(addr) != 1):
            raise DataException("Yaesu string formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("Yaesu string formats must of a zero bit"
                                + " offset")
        if ((offset % 8) != 0):
            raise DataException("Yaesu string formats must have a"
                                + " byte-multiple offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        s = self.data[byte_off:byte_off + numbytes]
        for i in range(0, len(s)):
            if (ord(s[i]) > 0x3f):
                s[i] = ' '
            else:
                s[i] = ys_chars[ord(s[i])]
                pass
            pass
        return s
    
    def set_int(self, vt, addr, offset):
        self.changed = True
        for a in addr.entries:
            byte_off = a[0]
            bit_off = a[1] + offset
            byte_off += bit_off / 8
            bit_off %= 8
            numbits = a[2]
            
            v = vt & ~(0xffffffff << numbits)
            vt >>= numbits
        
            x = ord(self.data[byte_off])
            if ((bit_off + numbits) <= 8):
                # Special case, all in one byte
                mask = (0xff >> (8 - numbits)) << bit_off
                x &= ~ mask
                x |= (v << bit_off) & mask
                self.data[byte_off] = x
                return
            mask = 0xff << bit_off
            x &= ~mask
            self.data[byte_off] = chr((v << byte_off) & mask)
            byte_off += 1
            v >>= bit_off
            numbits -= 8 - bit_off
            while (numbits >= 8):
                self.data[byte_off] = chr(v & 0xff)
                byte_off += 1
                v >>= 8
                numbits -= 8
                pass
            mask = 0xff << numbits
            x = ord(self.data[byte_off])
            x &= mask
            x |= v & ~mask;
            self.data[byte_off] = chr(x)
            pass
        pass

    # Yaesu BCD format for frequency, see FT60-layout.txt
    def set_bcd(self, v, addr, offset):
        if (len(addr) != 1):
            raise DataException("BCD formats only support one location field")
        if (addr[0][2] != 0):
            raise DataException("BCD formats must of a zero bit offset")
        if ((offset % 8) != 0):
            raise DataException("BCD formats must have a byte-multiple offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        if (numbytes == 0):
            return
        self.changed = True
        add5 = (v % 10) >= 5
        v /= 10
        byte_off += numbytes - 1
        while (numbytes > 0):
            self.data[byte_off] = chr((v % 10) + (((v / 10) % 10) * 10))
            v /= 100
            numbytes -= 1
            byte_off -= 1
            pass
        if (add5):
            x = ord(self.data[byte_off + 1])
            x |= 0x80
            self.data[byte_off + 1] = chr(x)
            pass
        pass

    def set_string(self, v, addr, offset):
        if (len(addr) != 1):
            raise DataException("String formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("String formats must of a zero bit offset")
        if ((offset % 8) != 0):
            raise DataException("String formats must have a byte-multiple"
                                + " offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        self.changed = True
        inlen = len(v)
        for i in range(0, numbytes):
            if (i >= inlen):
                c = " "
            else:
                c = v[i]
                pass
            if c not in ys_chars:
                c = " "
                pass
            self.data[byte_off] = c
            pass
        pass
    
    def set_yaesu_string(self, v, addr, offset):
        if (len(addr) != 1):
            raise DataException("Yaesu string formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("Yaesu string formats must of a zero bit"
                                + " offset")
        if ((offset % 8) != 0):
            raise DataException("Yaesu string formats must have a"
                                + " byte-multiple offset")
        byte_off = addr[0][0] + (offset / 8)
        numbytes = addr[0][2]

        self.changed = True
        inlen = len(v)
        for i in range(0, numbytes):
            if (i >= inlen):
                c = " "
            else:
                c = v[i]
                pass
            p = ys_chars.index(c)
            if (p < 0):
                p = chr(24)
            else:
                p = ys_chars[p]
                pass
            self.data[byte_off] = c
            pass
        pass
    
    pass


class BuiltIn:
    def __init__(self, name):
        self.name = name;
        pass
    pass

class Enum:
    def __init__(self, name):
        self.name = name
        self.entries = []
        pass

    def add(self, c, v):
        if (v[0] == "endenum"):
            c.addEnum(self)
            return
        if (len(v) != 2):
            raise ParseException(c.filename, c.lineno,
                                 "Invalid number of elements for enum");
        self.entries.append((c.toNum(v[0]), v[1]))
        pass

    pass


class List:
    def __init__(self, name, length):
        self.name = name
        self.length = length
        self.entries = []
        pass

    def add(self, c, v):
        if (v[0] == "endlist"):
            c.addList(self)
            return
        if (len(v) != 4):
            raise ParseException(c.filename, c.lineno,
                                 "Invalid number of elements for list entry");
        self.entries.append((v[0], Address(c, v[1]), c.toNum(v[2]),
                             c.findType(v[3])))
        pass

    pass


class Tab:
    def __init__(self, name):
        self.name = name
        self.entries = []
        pass

    def add(self, c, v):
        if (v[0] == "endtab"):
            c.addTab(self)
            return
        if (len(v) != 3):
            raise ParseException(c.filename, c.lineno,
                                 "Invalid number of elements for tab entry");
        self.entries.append((v[0], Address(c, v[1]), c.findType(v[2])))
        pass

    pass


class RadioConfig:
    def __init__(self, filename):
        self.filename = filename
        self.curr = None
        self.enums = []
        self.lists = []
        self.tabs = []
        self.builtins = [ BuiltIn("BCDFreq"), BuiltIn("IntFreq"),
                          BuiltIn("CheckBox"), BuiltIn("YaesuString"),
                          BuiltIn("String") ]
        f = open(filename, "r")
        try:
            self.lineno = 0
            while True:
                l = f.readline();
                if (not l):
                    break
                self.lineno += 1
                if l[0] == '#':
                    continue
                v = self.splitup_line(l);
                if (not v):
                    continue
                self.parseLine(v)
                pass
            pass
        except Exception, e:
            exceptionType, exceptionValue, exceptionTraceback = sys.exc_info()
            f.close()
            raise exceptionType, exceptionValue, exceptionTraceback
        f.close()
        pass

    def toNum(self, s):
        # Eliminate leading zeros
        i = 0
        l = len(s)
        if (l == 0):
            raise ParseException(self.filename, self.lineno,
                                 "Invalid number")
        while ((i < l) and (s[i] == '0')):
            i += 1
            pass
        if (i == l):
            return 0
        if ((i > 0) and (s[i] == 'x')):
            i -= 1
            pass
        try:
            v = int(s[i:], 0)
        except TypeError, e:
            raise ParseException(self.filename, self.lineno,
                                 "Invalid number")
        except ValueError, e:
            raise ParseException(self.filename, self.lineno,
                                 "Invalid number")
        return v

    def splitup_line(self, l):
        v = []
        instr = False
        inname = False
        i = 0
        last = len(l)
        start = 0
        while (i < last):
            c = l[i]
            if (c == '\\'):
                # Remove the '\', will automatically skip the char
                l = "".join([l[0:i], l[i+1:]])
                last -= 1
                if (i >= last):
                    raise ParseException(self.filename, self.lineno,
                                         "End of line after '\\'");
                i += 1
                continue
            if (instr and (c == '"')):
                v.append(l[start:i])
                instr = False
            elif (inname and c.isspace()):
                v.append(l[start:i])
                inname = False
            elif (c == '"'):
                start = i + 1
                instr = True
            elif (not (inname or instr) and not c.isspace()):
                start = i
                inname = True
                pass
            i += 1
            pass

        print str(v)
        if (instr):
            raise ParseException(self.filename, self.lineno,
                                 "End of line in string");
        if (inname):
            v.append(l[start:i])
        return v
    
    def parseLine(self, s):
        if (self.curr == None):
            if (s[0] == "enum"):
                if (len(s) < 1):
                    raise ParseException(self.filename, self.lineno,
                                         "Enum has no name")
                self.curr = Enum(s[1])
            elif (s[0] == "list"):
                if (len(s) < 1):
                    raise ParseException(self.filename, self.lineno,
                                         "List has no name")
                if (len(s) < 2):
                    raise ParseException(self.filename, self.lineno,
                                         "List has no length")

                length = self.toNum(s[2])
                self.curr = List(s[1], length)
            elif (s[0] == "tab"):
                if (len(s) < 1):
                    raise ParseException(self.filename, self.lineno,
                                         "Tab has no name")
                self.curr = Tab(s[1])
            else:
                raise ParseException(self.filename, self.lineno,
                                     "Invalid token: '" + s[0] + "'")
        else:
            self.curr.add(self, s)
            pass
        pass

    def addEnum(self, e):
        self.enums.append(e)
        self.curr = None
        pass

    def findType(self, s):
        for i in self.enums:
            if (i.name == s):
                return i
            pass
        for i in self.builtins:
            if (i.name == s):
                return i
            pass
        raise ParseException(self.filename, self.lineno, "Unknown type: " + s)
    
    def addList(self, e):
        self.lists.append(e)
        self.curr = None
        pass

    def addTab(self, e):
        self.tabs.append(e)
        self.curr = None
        pass

    pass


class GUI(Tix.Frame):
    def __init__(self, filename=None, master=None):
        Tix.Frame.__init__(self, master)
        self.top = master
        self.pack(fill=Tix.BOTH, expand=1);
        self.createWidgets();
        if (filename):
            self.openfile(filename)
            pass
        pass

    def openfile(self, filename):
        try:
            fd = RadioFileData(filename)
            radioname = find_radio(fd.data)
            radio = RadioConfig(radiodir + "/" + radioname + ".rad")
        except Exception, e:
            exceptionType, exceptionValue, exceptionTraceback = sys.exc_info()
            traceback.print_exception(exceptionType, exceptionValue,
                                      exceptionTraceback,
                                      file=sys.stdout)
            print str(e)
            pass
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

if (len(sys.argv) > 1):
    filename = sys.argv[1]
else:
    filename = None
    pass

root = Tix.Tk();
gui = GUI(filename, root);
gui.mainloop();
