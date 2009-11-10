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

    def get_bytes(self, addr, num):
        byte_pos = addr.entries[0][0] + (addr.entries[0][3] * num / 8)
        numbytes = addr.entries[0][2] / 8
        return self.data[byte_pos:byte_pos+numbytes]

    def set_byte(self, v, addr, num, offset):
        byte_pos = addr.entries[0][0] + (addr.entries[0][3] * num / 8)
        self.changed = True
        self.data[byte_pos + offset] = v
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
        h = Handler(None, None)
        h.widget = Tix.Label(parent)
        return h

    def renumWidget(self, h, num):
        pass

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
        h.num = num
        h.widget = w
        h.v = v
        if (v):
            w.select()
        else:
            w.deselect()
            pass
        return h

    def renumWidget(self, h, num):
        h.num = num
        t = h.data
        v = t.data.get_bits(t.addr, num)
        if (v):
            h.widget.select()
        else:
            h.widget.deselect()
            pass
        pass
        
    def set(self, h):
        w = h.widget
        t = h.data
        if (h.v):
            h.v = 0
        else:
            h.v = 1
            pass
        t.data.set_bits(h.v, t.addr, h.num)
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
        s = data.get_bytes(addr, num)
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
            data.set_byte(p, addr, num, i)
            pass
        pass

    def getWidget(self, parent, t, num):
        v = self.get_yaesu_string(t.data, t.addr, num)
        h = Handler(self.set, t)
        h.v = v
        h.num = num
        w = Tix.Entry(parent)
        h.widget = w
        w.bind("<KeyRelease>", h.set_event)
        w.delete(0, 'end')
        w.insert(0, v)
        return h

    def renumWidget(self, h, num):
        h.num = num
        t = h.data
        v = self.get_yaesu_string(t.data, t.addr, num)
        h.widget.delete(0, 'end')
        h.widget.insert(0, v)
        pass
        
    def set(self, h, event):
        w = h.widget
        t = h.data
        v = w.get()
        if (v == h.v):
            return
        cursor = w.index("insert")
        self.set_yaesu_string(v, t.data, t.addr, h.num)
        v = self.get_yaesu_string(t.data, t.addr, h.num)
        w.delete(0, 'end')
        w.insert(0, v)
        w.icursor(cursor)
        h.v = v
        pass
    pass

bcd_digits = "0123456789"
def convFromBCD(v):
    if (v >= 10) or (v < 0):
        return '0'
    return bcd_digits[v]

# Yaesu BCD format for frequency, see FT60-layout.txt
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
        s = data.get_bytes(addr, num)
        v = [ ]
        i = 0
        add5 = s[i] >> 4
        v.append(convFromBCD(s[i] & 0xf))
        i += 1
        numbytes = 3
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
        numbytes = addr.entries[0][2] / 8

        vlen = len(v)
        add5 = v[vlen - 1] >= '5'
        if (add5):
            d = 0x8
        else:
            d = 0
            pass
        second = True
        i = 0
        for c in v[0:vlen - 1]:
            if c == '.':
                continue
            if (second):
                d <<= 4
                d |= bcd_digits.find(c)
                second = False
                data.set_byte(d, addr, num, i)
                i += 1
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
        h.num = num
        w.bind("<Key>", h.set_event)
        w.delete(0, 'end')
        w.insert(0, v)
        return h

    def renumWidget(self, h, num):
        h.num = num
        t = h.data
        v = self.get_bcd(t.data, t.addr, num)
        h.widget.delete(0, 'end')
        h.widget.insert(0, v)
        pass
        
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
            self.set_bcd(w.get(), t.data, t.addr, h.num)
            pass
        if (cursor == 2):
            w.icursor(cursor + 2) # Skip over the '.'
        else:
            w.icursor(cursor + 1)
        return "break"

    pass

class MenuAndButton(Tix.Menubutton):
    def __init__(self, parent):
        Tix.Menubutton.__init__(self, parent)
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
        toph = Handler(None, None)
        toph.widget = MenuAndButton(parent)
        toph.data = t.data
        toph.addr = t.addr
        toph.num = num
        v = t.data.get_bits(t.addr, num)
        for e in self.entries:
            h = Handler(self.set, e)
            h.toph = toph
            toph.widget.add_command(e[1], h.set)
            if (v == e[0]):
                toph.widget.set_label(e[1])
                pass
            pass
        return toph

    def set(self, h):
        e = h.data
        h.toph.widget.set_label(e[1])
        h.toph.widget.data.set_bits(e[0], h.toph.addr, h.toph.num)
        pass

    def renumWidget(self, toph, num):
        toph.num = num
        v = toph.data.get_bits(toph.addr, num)
        for e in self.entries:
            if (v == e[0]):
                toph.widget.set_label(e[1])
                break
            pass
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

    def renumWidget(self, h, num):
        self.type.renumWidget(h, num)
    
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
        self.numlines = 0
        self.firstline = 0
        self.widgetlists = []
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

    def bindWidget(self, widget):
        widget.bind("<MouseWheel>", self.Wheel)
        widget.bind("<Down>", self.lineDown)
        widget.bind("<Up>", self.lineUp)
        widget.bind("<Next>", self.pageDown)
        widget.bind("<Prior>", self.pageUp)
        if (self.winsys == "x11"):
            widget.bind("<Button-4>", self.ButtonUp)
            widget.bind("<Button-5>", self.ButtonDown)
            pass
        pass
        
    def setup(self, top):
        try:
            self.winsys = top.tk.eval("return [ tk windowingsystem ]")
            pass
        except:
            # Assume x11
            self.winsys = "x11"
            pass

        self.xscroll = Tix.Scrollbar(top, orient=Tix.HORIZONTAL)
        self.yscroll = Tix.Scrollbar(top, orient=Tix.VERTICAL)
        self.list = Tix.HList(top, header=1, columns=len(self.entries) + 1,
                              itemtype="text", selectforeground="black",
                              selectbackground="beige",
                              yscrollcommand=self.y_scrolled,
                              xscrollcommand=self.xscroll.set)
        self.xscroll['command'] = self.list.xview
        self.yscroll['command'] = self.y_scrollbar_change
        self.xscroll.pack(side=Tix.BOTTOM, fill=Tix.X)
        self.yscroll.pack(side=Tix.RIGHT, fill=Tix.Y)

        self.bindWidget(self.list)
        self.bindWidget(self.xscroll)
        self.bindWidget(self.yscroll)
        
        i = 1
        for e in self.entries:
            e.column = i
            self.list.header_create(i, text=e.name)
            self.list.column_width(i, "")
            i += 1
            pass

        self.list.pack(side=Tix.LEFT, fill=Tix.BOTH, expand=1)
        pass

    def Wheel(self, event):
        self.list.yview("scroll", -(event.delta / 20), "units")
        return
    
    def ButtonUp(self, event):
        event.delta = 120
        self.Wheel(event);
        return
    
    def ButtonDown(self, event):
        event.delta = -120
        self.Wheel(event);
        return
    
    def y_scrolled(self, a, b):
        b = float(b)
        if (self.numlines < self.length) and (b >= 1.0):
            self.add_one_line()
        elif (self.numlines > 0) and (b < (1.0 - (1.0 / float(self.numlines)))):
            self.del_one_line()
        else:
            pct = float(self.numlines) / float(self.length)
            first = float(self.firstline) / float(self.length)
            self.yscroll.set(first, first + pct)
            pass
        pass

    def pageDown(self, event=None):
        self.y_scrollbar_change("scroll", 1, "pages")
        return "break"
    
    def pageUp(self, event=None):
        self.y_scrollbar_change("scroll", -1, "pages")
        return "break"
    
    def lineDown(self, event=None):
        l = self.list.info_selection()
        l = int(l[len(l)-1])
        if (l + 1 < self.numlines):
            self.list.selection_clear()
            self.list.selection_set(l + 1)
        else:
            self.y_scrollbar_change("scroll", 1, "units")
        return "break"
    
    def lineUp(self, event=None):
        l = self.list.info_selection()
        l = int(l[len(l)-1])
        if (l > 0):
            self.list.selection_clear()
            self.list.selection_set(l - 1)
        else:
            self.y_scrollbar_change("scroll", -1, "units")
            pass
        return "break"
    
    def y_scrollbar_change(self, a, b=None, c=None):
        if (a == "scroll"):
            if (c == "units"):
                if (int(b) < 0):
                    if (self.firstline > 0):
                        self.firstline -= 1
                        self.redisplay()
                        pass
                    pass
                else:
                    if ((self.firstline + self.numlines) < self.length):
                        self.firstline += 1
                        self.redisplay()
                        pass
                    pass
                pass
            elif (c == "pages"):
                if (int(b) < 0):
                    if (self.firstline > (self.numlines - 1)):
                        self.firstline -= self.numlines - 1
                        self.redisplay()
                    elif (self.firstline > 0):
                        self.firstline = 0
                        self.redisplay()
                        pass
                    pass
                else:
                    if (self.firstline + (2 * self.numlines) - 1 <= self.length):
                        self.firstline += self.numlines - 1
                        self.redisplay()
                    elif (self.firstline + self.numlines <= self.length):
                        self.firstline = self.length - self.numlines
                        self.redisplay()
                        pass
                    pass
                pass
        elif (a == "moveto"):
            pct = float(b)
            fl = int(pct * float(self.length))
            if ((fl != self.firstline)
                and (fl >= 0)
                and ((fl + self.numlines) <= self.length)):
                self.firstline = fl
                self.redisplay()
                pass
            pass
        pass

    def redisplay(self):
        i = 0
        for wl in self.widgetlists:
            self.list.item_configure(i, 0, text=str(i + self.firstline + 1))
            j = 0
            for e in self.entries:
                e.renumWidget(wl[j], i + self.firstline)
                j += 1
            i += 1
            pass
        pass
    
    def add_one_line(self):
        widgets = []
        pos = self.firstline + self.numlines
        self.list.add(self.numlines, text=str(pos + 1))
        for e in self.entries:
            h = e.getWidget(self.list, pos)
            self.bindWidget(h.widget)
            self.list.item_create(self.numlines, e.column,
                                  itemtype=Tix.WINDOW,
                                  window=h.widget)
            widgets.append(h)
            pass
        self.widgetlists.append(widgets)
        self.numlines += 1
        pass
        
    def del_one_line(self):
        if (self.numlines < 0):
            return
        self.numlines -= 1
        del self.widgetlists[self.numlines]
        self.list.delete_entry(self.numlines)
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
            h = e.getWidget(self.list.hlist, 0)
            self.list.hlist.item_create(i, 1, itemtype=Tix.WINDOW,
                                        window=h.widget)
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
