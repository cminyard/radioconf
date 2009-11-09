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
                    if (data[i] != n):
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
                                 "Address doesn't start with (");
        if (s[l-1] != ')'):
            raise ParseException(c.filename, c.lineno,
                                 "Address doesn't end with )");
        self.entries = []
        i = 1
        l -= 1
        pos = 0
        start = 1
        v = [0, 0, 0, 0]
        self.entries.append(v)
        while (i < l):
            if (s[i] == ','):
                v[pos] = c.toNum(s[start:i])
                start = i + 1
                pos += 1
                if (pos > 3):
                    raise ParseException(c.filename, c.lineno,
                                         "Address has too many values"
                                         + " in a field");
                pass
            elif (s[i] == ':'):
                if (pos < 2 or pos > 3):
                    raise ParseException(c.filename, c.lineno,
                                         "Address has too few values"
                                         + " in a field");
                v[pos] = c.toNum(s[start:i])
                start = i + 1
                pos = 0
                v = [0, 0, 0, 0]
                self.entries.append(v)
                pass
            i += 1
            pass
        if (pos < 2 or pos > 3):
            raise ParseException(c.filename, c.lineno,
                                 "Address has too few values"
                                 + " in a field");
        v[pos] = c.toNum(s[start:i])
    pass


class RadioFileData:
    # Throws IOError
    def __init__(self, filename):
        self.filename = filename
        f = open(filename, "rb")
        strdata = f.read();
        f.close()
        self.data = []
        for c in strdata:
            self.data.append(ord(c))
        self.changed = False
        pass

    def write(self, filename=None):
        if (filename == None):
            filename = self.filename
            pass
        outs = []
        for c in self.data:
            outs.append(chr(c))
            pass
        s = "".join(outs)
        try:
            f = open(filename, "wb")
            f.write(s)
        finally:
            f.close()
            pass
        pass

    # The get and set routine throw IndexError if out of range.
    
    def get_bits(self, addr, num):
        v = 0
        for a in addr.entries:
            byte_off = a[0]
            bit_off = a[1] + a[3] * num
            byte_off += bit_off / 8
            bit_off %= 8
            numbits = a[2]
            v <<= numbits
            
            x = self.data[byte_off] >> bit_off
            bits = 8 - bit_off
            shift = bit_off
            while (bits < numbits):
                byte_off += 1
                x |= self.data[byte_off] << shift
                shift += 8
                bits += 8
                pass
            v |= x & ~(0xffffffff << numbits)
            pass
        return v

    def set_bits(self, vt, addr, num):
        self.changed = True
        bitsleft = 0
        for a in addr.entries:
            bitsleft += a[2]
            pass
        for a in addr.entries:
            byte_off = a[0]
            bit_off = a[1] + a[3] * num
            byte_off += bit_off / 8
            bit_off %= 8
            numbits = a[2]
            bitsleft -= numbits

            v = vt >> bitsleft
            v = v & ~(0xffffffff << numbits)
        
            x = self.data[byte_off]
            if ((bit_off + numbits) <= 8):
                # Special case, all in one byte
                mask = (0xff >> (8 - numbits)) << bit_off
                x &= ~ mask
                x |= (v << bit_off) & mask
                self.data[byte_off] = x
                continue
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
        pass

    def get_bytes(self, addr, len):
        return self.data[addr:addr+len]

    def set_bytes(self, v, addr):
        self.changed = True
        self.data[addr:addr+len(v)] = v
        pass

    def set_byte(self, v, addr):
        self.changed = True
        self.data[addr] = v
        pass

    def get_string(self, addr, num):
        if (len(addr) != 1):
            raise DataException("String formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("String formats must of a zero bit offset")
        if ((addr[0][3] % 8) != 0):
            raise DataException("String formats must have a byte-multiple"
                                + " offset")
        byte_off = addr[0][0] + (addr[0][3] * num / 8)
        numbytes = addr[0][2]

        s = []
        for i in (0, numbyte):
            s.append(chr(self.data[byte_off+i]))
            pass
        for i in range(0, len(s)):
            if (s[i] not in ys_chars):
                if (s[i].islower()):
                    s[i] = s[i].upper()
                    continue
                s[i] = ' '
                pass
            pass
        s = "".join(s)
        return s
    
    def set_string(self, v, addr, num):
        if (len(addr) != 1):
            raise DataException("String formats only support one"
                                + " location field")
        if (addr[0][2] != 0):
            raise DataException("String formats must of a zero bit offset")
        if ((offset % 8) != 0):
            raise DataException("String formats must have a byte-multiple"
                                + " offset")
        byte_off = addr[0][0] + (addr[0][3] * num / 8)
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
            self.data[byte_off] = ord(c)
            byte_off += 1
            pass
        pass
    
    pass


class Handler:
    """This is an internal type that eases handling events for a type.
    The type objects do not own the data about the particular object
    they are used to represent, since they are reused fo rmany
    different objects.  Instead, when a widget is allocated to handle
    a particular type, one of these is allocated to hold that data and
    deliver it to the type object."""
    def __init__(self, handler, data):
        self.handler = handler
        self.data = data
        pass

    def set(self):
        self.handler(self)
        pass

    def set_event(self, event):
        return self.handler(self, event)

    pass

class BuiltIn:
    def __init__(self, name):
        self.name = name;
        pass

    def getWidget(self, parent, t, num):
        w = Tix.Label(parent)
        return w

    def checkAddrOk(self, c, addr):
        return True

    pass

class BICheckBox(BuiltIn):
    def __init__(self):
        BuiltIn.__init__(self, "CheckBox")
        pass

    def getWidget(self, parent, t, num):
        v = t.data.get_bits(t.addr, num)
        h = Handler(self.set, t)
        w = Tix.Checkbutton(parent, command=h.set)
        h.BIrownum = num
        h.widget = w
        h.v = v
        if (v):
            w.select()
        else:
            w.deselect()
            pass
        return w

    def set(self, h):
        w = h.widget
        t = h.data
        if (h.v):
            h.v = 0
        else:
            h.v = 1
            pass
        t.data.set_bits(h.v, t.addr, h.BIrownum)
        pass

# These are internal Yaesu string values for some radios.
ys_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ !\"\\$#%'()*+,-;/|:<=>?@[&]^_"

class BIYaesuString(BuiltIn):
    def __init__(self):
        BuiltIn.__init__(self, "YaesuString")
        pass

    def checkAddrOk(self, c, addr):
        if (len(addr.entries) != 1):
            raise ParseException(c.filename, c.lineno,
                                 "Yaesu string formats only support one"
                                + " location field")
        if (addr.entries[0][1] != 0):
            raise ParseException(c.filename, c.lineno,
                                 "Yaesu string formats must have a zero bit"
                                 + " offset")
        if ((addr.entries[0][2] % 8) != 0):
            raise ParseException(c.filename, c.lineno,
                                 "Yaesu string formats must be a"
                                 + " byte-multiple size")
        if ((addr.entries[0][3] % 8) != 0):
            raise ParseException(c.filename, c.lineno,
                                 "Yaesu string formats must have a"
                                 + " byte-multiple offset")
        return True

    def get_yaesu_string(self, data, addr, num):
        byte_off = addr.entries[0][0] + (addr.entries[0][3] * num / 8)
        numbytes = addr.entries[0][2] / 8

        s = data.get_bytes(byte_off, numbytes)
        for i in range(0, len(s)):
            if (s[i] > 0x3f):
                s[i] = ' '
            else:
                s[i] = ys_chars[s[i]]
                pass
            pass
        s = "".join(s)
        return s

    def set_yaesu_string(self, v, data, addr, num):
        byte_off = addr.entries[0][0] + (addr.entries[0][3] * num / 8)
        numbytes = addr.entries[0][2] / 8

        inlen = len(v)
        for i in range(0, numbytes):
            if (i >= inlen):
                p = 0x24
            else:
                p = ys_chars.find(v[i].upper())
                if (p < 0):
                    p = 0x24
                    pass
                pass
            data.set_byte(p, byte_off)
            byte_off += 1
            pass
        pass

    def getWidget(self, parent, t, num):
        v = self.get_yaesu_string(t.data, t.addr, num)
        h = Handler(self.set, t)
        h.v = v
        h.BIrownum = num
        w = Tix.Entry(parent)
        h.widget = w
        w.bind("<KeyRelease>", h.set_event)
        w.delete(0, 'end')
        w.insert(0, v)
        return w

    def set(self, h, event):
        w = h.widget
        t = h.data
        v = w.get()
        if (v == h.v):
            return
        cursor = w.index("insert")
        self.set_yaesu_string(v, t.data, t.addr, h.BIrownum)
        v = self.get_yaesu_string(t.data, t.addr, h.BIrownum)
        w.delete(0, 'end')
        w.insert(0, v)
        w.icursor(cursor)
        h.v = v
        pass
    pass

# Yaesu BCD format for frequency, see FT60-layout.txt
bcd_digits = "0123456789"
def convFromBCD(v):
    if (v >= 10) or (v < 0):
        return '0'
    return bcd_digits[v]

class BIBCDFreq(BuiltIn):
    def __init__(self):
        BuiltIn.__init__(self, "BCDFreq")
        pass

    def checkAddrOk(self, c, addr):
        if (len(addr.entries) != 1):
            raise ParseException(c.filename, c.lineno,
                                 "BCDFreq formats only support one"
                                + " location field")
        if (addr.entries[0][1] != 0):
            raise ParseException(c.filename, c.lineno,
                                 "BCDFreq formats must have a zero bit"
                                 + " offset")
        if (addr.entries[0][2] != 24):
            raise ParseException(c.filename, c.lineno,
                                 "BCDFreq formats must be 24 bits long")
        if ((addr.entries[0][3] % 8) != 0):
            raise ParseException(c.filename, c.lineno,
                                 "BCDFreq formats must have a"
                                 + " byte-multiple offset")
        return True

    def get_bcd(self, data, addr, num):
        byte_off = addr.entries[0][0] + (addr.entries[0][3] * num / 8)
        numbytes = addr.entries[0][2] / 8

        s = data.get_bytes(byte_off, numbytes)
        v = [ ]
        i = 0
        add5 = s[i] >> 4
        v.append(convFromBCD(s[i] & 0xf))
        i += 1
        while (i < numbytes):
            v.append(convFromBCD(s[i] >> 4))
            v.append(convFromBCD(s[i] & 0xf))
            i += 1
            pass
        if (add5):
            v.append('5')
        else:
            v.append('0')
            pass
        v.insert(3, '.')
        return "".join(v)
        pass

    # Yaesu BCD format for frequency, see FT60-layout.txt
    def set_bcd(self, v, data, addr, num):
        byte_off = addr.entries[0][0] + (addr[0][3] * num / 8)
        numbytes = addr.entries[0][2] / 8

        vlen = len(v)
        add5 = v[vlen - 1] >= '5'
        if (add5):
            d = 0x8
        else:
            d = 0
            pass
        second = True
        for c in v[0:vlen - 1]:
            if c == '.':
                continue
            if (second):
                d <<= 4
                d |= bcd_digits.find(c)
                second = False
                data.set_byte(d, byte_off)
                byte_off += 1
                pass
            else:
                d = bcd_digits.find(c)
                second = True
                pass
            pass
        pass

    def getWidget(self, parent, t, num):
        v = self.get_bcd(t.data, t.addr, num)
        h = Handler(self.set, t)
        w = Tix.Entry(parent)
        h.widget = w
        h.BIrownum = num
        w.bind("<Key>", h.set_event)
        w.delete(0, 'end')
        w.insert(0, v)
        return w

    def set(self, h, event):
        # FIXME - handling pasting
        if ((event.keysym == "BackSpace") or (event.keysym == "Delete")
            or (event.keysym == "Insert")):
            return "break" # Don't allow deletions
        if (len(event.keysym) > 1):
            return # Let other key editing through

        # Now we have normal character keys.  Ignore everything but
        # digits, don't go past the end or change the "."

        w = h.widget
        cursor = w.index("insert")
        if (cursor >= 7):
            return "break" # Past the end of the entry
        if (cursor == 3):
            w.icursor(cursor + 1) # Skip over the '.'
            return "break"

        c = event.keysym
        if (c not in bcd_digits):
            return "break" # Ignore everything but numbers

        if (cursor == 6):
            # Last digit can only be 0 or 5
            if (c < '5'):
                c = '0'
            else:
                c = '5'
                pass
            pass
                
        s = w.get()
        if (s[cursor] != c):
            w.delete(cursor)
            w.insert(cursor, c)
            self.set_bcd(w.get(), t.data, t.addr, h.BIrownum)
            pass
        if (cursor == 2):
            w.icursor(cursor + 2) # Skip over the '.'
        else:
            w.icursor(cursor + 1)
        return "break"

    pass

class MenuAndButton(Tix.Menubutton):
    def __init__(self, parent, data, addr, num):
        Tix.Menubutton.__init__(self, parent)
        self.data = data
        self.addr = addr
        self.num = num
        self.menu = Tix.Menu(self)
        self['menu'] = self.menu
        pass

    def add_command(self, name, handler):
        self.menu.add_command(label=name, command=handler)
        pass

    def set_label(self, label):
        self["text"] = label
    pass

class Enum(BuiltIn):
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

    def getWidget(self, parent, t, num):
        self.button = MenuAndButton(parent, t.data, t.addr, num)
        v = t.data.get_bits(t.addr, num)
        for e in self.entries:
            h = Handler(self.set, e)
            h.BIrownum = num
            self.button.add_command(e[1], h.set)
            if (v == e[0]):
                self.button.set_label(e[1])
                pass
            pass
        return self.button

    def set(self, h):
        e = h.data
        self.button.set_label(e[1])
        self.button.data.set_bits(e[0], self.button.addr, self.button.num)
        pass

    pass


class TabEntry:
    def __init__(self, name, addr, type, data):
        self.name = name
        self.addr = addr
        self.type = type
        self.data = data
        pass

    def getWidget(self, parent, num):
        return self.type.getWidget(parent, self, num)
    
    pass

class TabBase:
    def __init__(self, name):
        self.name = name
        self.entries = []
        pass

    def addItem(self, c, name, addr, type):
        type.checkAddrOk(c, addr)
        self.entries.append(TabEntry(name, addr, type, c.filedata))
        pass

class List(TabBase):
    def __init__(self, name, length):
        TabBase.__init__(self, name)
        self.length = length
        pass

    def add(self, c, v):
        if (v[0] == "endlist"):
            c.addList(self)
            return
        if (len(v) != 3):
            raise ParseException(c.filename, c.lineno,
                                 "Invalid number of elements for list entry");
        self.addItem(c, v[0], Address(c, v[1]), c.findType(v[2]))
        pass

    def setup(self, top):
        self.list = Tix.ScrolledHList(top, scrollbar="auto",
                                      options="hlist.header 1"
                                      + " hlist.columns "
                                      + str(len(self.entries) + 1)
                                      + " hlist.itemtype text"
                                      + " hlist.selectForeground black"
                                      + " hlist.selectBackground beige")
        i = 1
        for e in self.entries:
            e.column = i
            self.list.hlist.header_create(i, text=e.name)
            self.list.hlist.column_width(i, "")
            i += 1
            pass

        for i in range(0, self.length):
            print ("Adding line " + str(i))
            self.list.hlist.add(i, text=str(i+1))
            for e in self.entries:
                w = e.getWidget(self.list.hlist, i)
                self.list.hlist.item_create(i, e.column,
                                            itemtype=Tix.WINDOW, window=w)
                pass
            pass
        
        self.list.pack(side=Tix.LEFT, fill=Tix.BOTH, expand=1)
        pass
    pass

class Tab(TabBase):
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
        self.addItem(c, v[0], Address(c, v[1]), c.findType(v[2]))
        pass

    def setup(self, top):
        self.list = Tix.ScrolledHList(top, scrollbar="auto",
                                      options="hlist.header 1"
                                      + " hlist.columns 2"
                                      + " hlist.itemtype text"
                                      + " hlist.selectForeground black"
                                      + " hlist.selectBackground beige")
        self.list.hlist.header_create(0, text="Name")
        self.list.hlist.header_create(1, text="Value")
        self.list.hlist.column_width(0, "")
        self.list.hlist.column_width(1, "")

        i = 0
        for e in self.entries:
            e.key = i
            self.list.hlist.add(i, text=e.name)
            w = e.getWidget(self.list.hlist, 0)
            self.list.hlist.item_create(i, 1, itemtype=Tix.WINDOW, window=w)
            i += 1
            pass

        self.list.pack(side=Tix.LEFT, fill=Tix.BOTH, expand=1)

        pass
    pass

quote_hash = {"n" : "\n",
              "t" : "\t",
              "r" : "\r" }

def unquote(s):
    inquote = False
    v = []
    for c in s:
        if (inquote):
            if (c in quote_hash):
                v.append(quote_hash[c])
            else:
                v.append(c);
                pass
            inquote = False
            pass
        elif (c == '\\'):
            inquote = True
            pass
        else:
            v.append(c)
            pass
        pass
    return "".join(v)

class RadioConfig:
    def __init__(self, filename, filedata):
        self.filename = filename
        self.filedata = filedata
        self.curr = None
        self.toplevel = []
        self.types = [ BIBCDFreq(), BuiltIn("IntFreq"),
                       BICheckBox(), BIYaesuString(),
                       BuiltIn("String"), BuiltIn("Empty") ]
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
        inquote = False
        i = 0
        last = len(l)
        start = 0
        while (i < last):
            c = l[i]
            if (inquote):
                inquote = False
                pass
            elif (instr and (c == '"')):
                v.append(unquote(l[start:i]))
                instr = False
            elif (inname and c.isspace()):
                v.append(unquote(l[start:i]))
                inname = False
            elif (c == '"'):
                start = i + 1
                instr = True
            elif (not (inname or instr) and not c.isspace()):
                if (c == '\\'):
                    inquote = True
                    pass
                start = i
                inname = True
            elif (c == '\\'):
                inquote = True
                pass
            i += 1
            pass

        if (inquote):
            raise ParseException(self.filename, self.lineno,
                                 "End of line after '\\'");
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
        for i in self.types:
            if (i.name == e.name):
                raise ParseException(self.filename, self.lineno,
                                     "Duplicate type: " + e.name)
            pass
        self.types.append(e)
        self.curr = None
        pass

    def findType(self, s):
        for i in self.types:
            if (i.name == s):
                return i
            pass
        raise ParseException(self.filename, self.lineno, "Unknown type: " + s)
    
    def addList(self, e):
        self.toplevel.append(e)
        self.curr = None
        pass

    def addTab(self, e):
        self.toplevel.append(e)
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
            self.fd = RadioFileData(filename)
            self.radioname = find_radio(self.fd.data)
            self.radio = RadioConfig(radiodir + "/" + self.radioname + ".rad",
                                     self.fd)
        except Exception, e:
            exceptionType, exceptionValue, exceptionTraceback = sys.exc_info()
            traceback.print_exception(exceptionType, exceptionValue,
                                      exceptionTraceback,
                                      file=sys.stdout)
            print str(e)
            return
        for t in self.radio.toplevel:
            t.tab = self.tabs.add(t.name.lower(), label=t.name)
            t.setup(t.tab)
            pass
        pass
    
    def quitcmd(self, event=None):
        self.quit();
        pass

    def opencmd(self, event=None):
        print "Open!\n"
        pass

    def savecmd(self, event=None):
        self.fd.write()
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
        self.filemenu.add_command(label="Save", underline=1,
                                  accelerator="Ctrl+S",
                                  command = lambda self=self: self.savecmd() )
        self.filemenu.add_command(label="Exit", underline=1,
                                  accelerator="Ctrl+Q",
                                  command = lambda self=self: self.quitcmd() )
        self.top.bind_all("<Control-Q>", self.quitcmd)
        self.top.bind_all("<Control-q>", self.quitcmd)
        self.top.bind_all("<Control-S>", self.savecmd)
        self.top.bind_all("<Control-s>", self.savecmd)
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

        pass

if (len(sys.argv) > 1):
    filename = sys.argv[1]
else:
    filename = None
    pass

root = Tix.Tk();
gui = GUI(filename, root);
gui.mainloop();
