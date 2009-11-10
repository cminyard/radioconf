#!/usr/bin/env python

import sys
import traceback
import Tix

radiodir = "/etc/yaesuconf"

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

class RadioInfo:
    def __init__(self, name):
        self.name = name;
        self.headercmp = []
        return

    pass

radios = []

def find_radio(data):
    for r in radios:
        if (len(data) < len(r.headercmp)):
            continue
        found = True
        i = 0
        for b in r.headercmp:
            if (b != data[i]):
                found = False
                break
            i += 1
            pass
        if (found):
            return r.name
        pass
    return None

# Throws IOError and ParseException
def read_radios():
    filename = radiodir + "/radios"
    f = open(filename, "r");
    try:
        lineno = 0
        in_radio = False
        l = ""
        while True:
            l += f.readline()
            if (not l):
                break;
            lineno += 1
            if l[len(l) - 1] == '\\':
               continue 
            if l[0] == '#':
                l = ""
                continue
            v = l.split();
            l = ""
            if (not v):
                continue
            if (v[0] == "radio"):
                if (in_radio):
                    raise ParseException(filename, lineno,
                                         "Got radio start inside a radio")
                if (len(v) < 2):
                    raise ParseException(filename, lineno,
                                         "No radio specified")
                curr_radio = RadioInfo(v[1])
                in_radio = True
                continue
            if (v[0] == "endradio"):
                if (not in_radio):
                    raise ParseException(filename, lineno,
                                         "Got radio end outside a radio")
                if (len(curr_radio.headercmp) == 0):
                    raise ParseException(filename, lineno,
                                         "No headercmp for radio")
                radios.append(curr_radio)
                in_radio = False
                continue
            if (v[0] == "headercmp"):
                if (not in_radio):
                    raise ParseException(filename, lineno,
                                         "Got headercmp outside a radio")
                try:
                    i = 0
                    found = True
                    for ns in v[1:]:
                        n = int(ns, 16)
                        if (n > 255):
                            raise TypeError("Number too large")
                        curr_radio.headercmp.append(n)
                        i += 1
                        pass
                    pass
                except TypeError, e:
                    raise ParseException(filename, lineno,
                                         "invalid hexadecimal 8-bit number")
                except ValueError, e:
                    raise ParseException(filename, lineno,
                                         "invalid hexadecimal 8-bit number")
                pass
            pass
        pass
    except:
        exceptionType, exceptionValue, exceptionTraceback = sys.exc_info()
        f.close()
        raise exceptionType, exceptionValue, exceptionTraceback
    return

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
        else:
            self.filename = filename
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
        self.changed = False
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
        self.name = name

        # A set of alternate names for this type, where selection might
        # occur from
        self.altnames = []
        pass

    def getWidget(self, parent, t, num):
        h = Handler(None, None)
        h.widget = Tix.Label(parent)
        return h

    def renumWidget(self, h, num):
        pass

    def checkAddrOk(self, c, addr):
        return True

    def getSelect(self, data, addr, num):
        return ""
    
    def setValue(self, v, data, addr, num):
        pass
    
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

    def getSelect(self, data, addr, num):
        v = data.get_bits(addr, num)
        if (v):
            return '1'
        else:
            return '0'
    
    def setValue(self, v, data, addr, num):
        if (v == "0"):
            data.set_bits(0, addr, num)
        elif (v == "1"):
            data.set_bits(1, addr, num)
            pass
        pass
    
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
        
    def getSelect(self, data, addr, num):
        return self.get_yaesu_string(data, addr, num)

    def setValue(self, v, data, addr, num):
        self.set_yaesu_string(v, data, addr, num)
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
        
    def getSelect(self, data, addr, num):
        return self.get_bcd(data, addr, num)

    def set(self, h, event):
        if ((event.keysym == "BackSpace") or (event.keysym == "Delete")
            or (event.keysym == "Insert") or (event.keysym == "space")):
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

    def setValue(self, v, data, addr, num):
        for c in v:
            if ((c != '.') and (c not in bcd_digits)):
                return
            pass

        self.set_bcd(v, data, addr, num)
        pass
    pass

hex_digits = "0123456789ABCDEF "
def convFromHex(v):
    if (v == 0xff):
        return ' '
    if (v >= 16) or (v < 0):
        return '0'
    return hex_digits[v]

class BIHex(BuiltIn):
    def __init__(self):
        BuiltIn.__init__(self, "HexDigits")
        pass

    def checkAddrOk(self, c, addr):
        if (len(addr.entries) != 1):
            raise ParseException(c.filename, c.lineno,
                                 "HexDigit formats only support one"
                                + " location field")
        if (addr.entries[0][1] != 0):
            raise ParseException(c.filename, c.lineno,
                                 "HexDigits formats must have a zero bit"
                                 + " offset")
        if ((addr.entries[0][2] % 8) != 0):
            raise ParseException(c.filename, c.lineno,
                                 "HexDigits formats must have a"
                                 + " byte-multiple size")
        if ((addr.entries[0][3] % 8) != 0):
            raise ParseException(c.filename, c.lineno,
                                 "HexDigits formats must have a"
                                 + " byte-multiple offset")
        return True

    def get_hex(self, data, addr, num):
        s = data.get_bytes(addr, num)
        v = [ ]
        i = 0
        numbytes = addr.entries[0][2] / 8
        while (i < numbytes):
            v.append(convFromHex(s[i]))
            i += 1
            pass
        return "".join(v)
        pass

    def set_hex(self, v, data, addr, num):
        numbytes = addr.entries[0][2] / 8

        vlen = len(v)
        second = False
        i = 0
        for c in v:
            if (c == ' '):
                d = 0xff
            else:
                d = hex_digits.find(c)
            data.set_byte(d, addr, num, i)
            i += 1
            pass
        pass

    def getWidget(self, parent, t, num):
        v = self.get_hex(t.data, t.addr, num)
        h = Handler(self.set, t)
        h.numbytes = t.addr.entries[0][2] / 8
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
        v = self.get_hex(t.data, t.addr, num)
        h.widget.delete(0, 'end')
        h.widget.insert(0, v)
        pass
        
    def getSelect(self, data, addr, num):
        return self.get_hex(data, addr, num)

    def set(self, h, event):
        print event.keysym
        if ((event.keysym == "BackSpace") or (event.keysym == "Delete")
            or (event.keysym == "Insert")):
            return "break" # Don't allow deletions
        if (event.keysym == "space"):
            event.keysym = ' '
        elif (len(event.keysym) > 1):
            return # Let other key editing through

        # Now we have normal character keys.  Ignore everything but
        # digits, don't go past the end or change the "."

        w = h.widget
        cursor = w.index("insert")
        if (cursor >= h.numbytes):
            return "break" # Past the end of the entry

        c = event.keysym.upper()
        if (c != ' ' and c not in hex_digits):
            return "break" # Ignore everything but numbers and space

        t = h.data
        s = w.get()
        if (s[cursor] != c):
            w.delete(cursor)
            w.insert(cursor, c)
            self.set_hex(w.get(), t.data, t.addr, h.num)
            pass
        w.icursor(cursor + 1)
        return "break"

    def setValue(self, v, data, addr, num):
        for c in v:
            if (c not in bcd_digits):
                return
            pass

        self.set_hex(v, data, addr, num)
        pass
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

class EnumEntry:
    def __init__(self, value, str):
        self.value = value
        self.str = str
        return
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
        elif (v[0] == "altname"):
            if (len(v) < 2):
                raise ParseException(c.filename, c.lineno,
                                     "No alternate name given");
            self.altnames.append(v[1])
            return
        
        if (len(v) != 2):
            raise ParseException(c.filename, c.lineno,
                                 "Invalid number of elements for enum");
        self.entries.append(EnumEntry(c.toNum(v[0]), v[1]))
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
            toph.widget.add_command(e.str, h.set)
            if (v == e.value):
                toph.widget.set_label(e.str)
                pass
            pass
        return toph

    def set(self, h):
        e = h.data
        h.toph.widget.set_label(e.str)
        h.toph.data.set_bits(e.value, h.toph.addr, h.toph.num)
        pass

    def renumWidget(self, toph, num):
        toph.num = num
        v = toph.data.get_bits(toph.addr, num)
        for e in self.entries:
            if (v == e.value):
                toph.widget.set_label(e.str)
                break
            pass
        pass

    def getSelect(self, data, addr, num):
        v = data.get_bits(addr, num)
        for e in self.entries:
            if (v == e.value):
                return e.str.replace(" ", "")
            pass
        pass

    def setValue(self, v, data, addr, num):
        for e in self.entries:
            if (v == e.value.replace(" ", "")):
                data.set_bits(e.str, addr, num)
                return
            pass
        pass
    
    pass


class TabEntry:
    def __init__(self, name, addr, type, data):
        self.name = name
        nn = self.name.replace("\n", "")
        nn = nn.replace(" ", "")
        nn = nn.replace("\t", "")
        self.nsname = nn
        self.addr = addr
        self.type = type
        self.data = data
        pass

    def getWidget(self, parent, num):
        return self.type.getWidget(parent, self, num)

    def renumWidget(self, h, num):
        self.type.renumWidget(h, num)
        pass

    def getSelect(self, num):
        v = self.type.getSelect(self.data, self.addr, num)
        if (v is None):
            v = ""
        return (self.nsname + "=" + v)

    def setSelect(self, num, vhash):
        if (self.nsname in vhash):
            v = vhash[self.nsname]
            del vhash[self.nsname]
            self.type.setValue(v, self.data, self.addr, num)
            pass
        else:
            for n in self.type.altnames:
                if (n in vhash):
                    v = vhash[n]
                    del vhash[n]
                    self.type.setValue(v, self.data, self.addr, num)
                    return
                pass
            pass
        pass
    
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
            widget.bind("<Button-4>", self.ButtonUpWheel)
            widget.bind("<Button-5>", self.ButtonDownWheel)
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
        self.yscroll['command'] = self.y_move
        self.xscroll.pack(side=Tix.BOTTOM, fill=Tix.X)
        self.yscroll.pack(side=Tix.RIGHT, fill=Tix.Y)

        self.list.selection_handle(self.selection_request)

        self.first_select = -1
        self.last_select = -1
        self.list.bind("<Button-1>", self.Button1)
        self.list.bind("<Shift-Button-1>", self.SButton1)
        self.list.bind("<Control-Button-1>", self.CButton1)
        self.list.bind("<Button-2>", self.Button2)
        self.list.bind("<Button-3>", self.Button3)

        self.list.bind("<Map>", self.map)

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

    def map(self, event=None):
        self.list.focus_set()
        return
        
    def selection_request(self, a, b):
        if (self.first_select < 0):
            return ""
        s = ""
        for i in range(self.first_select, self.last_select + 1):
            if (s):
                s += "\n"
                pass
            first = True
            for e in self.entries:
                if (not first):
                    s += " "
                else:
                    first = False
                    pass
                s += e.getSelect(i)
                pass
            pass
        return s

    def redoSelection(self):
        if ((self.last_select < self.firstline)
            or (self.first_select >= self.firstline + self.numlines)):
            # Nothing to see
            self.list.selection_clear()
            return
        
        if (self.first_select <= self.firstline):
            pos1 = 0
        else:
            pos1 = self.first_select - self.firstline
            pass
        
        end = self.firstline + self.numlines - 1
        if (self.last_select >= end):
            pos2 = self.numlines - 1
        else:
            pos2 = self.last_select - self.firstline
            pass

        self.list.selection_clear()
        self.list.selection_set(pos1, pos2)
        self.list.selection_own()
        pass
    
    def Button1(self, event):
        self.list.focus_set()
        line = int(self.list.nearest(event.y))
        self.first_select = self.firstline + line
        self.last_select = self.firstline + line
        self.redoSelection()
        return "break"
    
    def SButton1(self, event):
        if (self.first_select < 0):
            return self.Button1(event)
        line = int(self.list.nearest(event.y))
        pos = line + self.firstline
        if (pos < self.first_select):
            self.first_select = pos
        else:
            self.last_select = pos
            pass
        self.redoSelection()
        return "break"
    
    def CButton1(self, event):
        return "break"
    
    def Button2(self, event):
        linenum = int(self.list.nearest(event.y)) + self.firstline
        s = self.list.selection_get()
        lines = s.split("\n")
        for l in lines:
            if (linenum >= self.length):
                return
            vhash = {}
            for vp in l.split():
                v = vp.split("=")
                if (len(v) != 2):
                    continue
                vhash[v[0]] = v[1]
                pass
            for e in self.entries:
                e.setSelect(linenum, vhash)
                pass
            linenum += 1
            pass
        self.redisplay()
        return "break"
    
    def Button3(self, event):
        self.first_select = -1
        self.last_select = -1
        self.redoSelection()
        return "break"
    
    def Wheel(self, event):
        self.y_move("scroll", -(event.delta / 20), "units")
        return
    
    def ButtonUpWheel(self, event):
        event.delta = 120
        self.Wheel(event);
        return
    
    def ButtonDownWheel(self, event):
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
        self.redoSelection()
        pass

    def pageDown(self, event=None):
        self.y_move("scroll", 1, "pages")
        return "break"
    
    def pageUp(self, event=None):
        self.y_move("scroll", -1, "pages")
        return "break"
    
    def lineDown(self, event=None):
        self.y_move("scroll", 1, "units")
        return "break"
    
    def lineUp(self, event=None):
        self.y_move("scroll", -1, "units")
        return "break"
    
    def y_move(self, a, b=None, c=None):
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

    def bindWidget(self, widget):
        widget.bind("<MouseWheel>", self.Wheel)
        if (self.winsys == "x11"):
            widget.bind("<Button-4>", self.ButtonUpWheel)
            widget.bind("<Button-5>", self.ButtonDownWheel)
            pass
        pass
        
    def Wheel(self, event):
        self.list.hlist.yview("scroll", -(event.delta / 20), "units")
        return
    
    def ButtonUpWheel(self, event):
        event.delta = 120
        self.Wheel(event);
        return
    
    def ButtonDownWheel(self, event):
        event.delta = -120
        self.Wheel(event);
        return
    
    def setup(self, top):
        try:
            self.winsys = top.tk.eval("return [ tk windowingsystem ]")
            pass
        except:
            # Assume x11
            self.winsys = "x11"
            pass

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
        self.bindWidget(self.list)
        self.bindWidget(self.list.hlist)
        self.bindWidget(self.list.hsb)
        self.bindWidget(self.list.vsb)

        i = 0
        for e in self.entries:
            e.key = i
            self.list.hlist.add(i, text=e.name)
            h = e.getWidget(self.list.hlist, 0)
            self.list.hlist.item_create(i, 1, itemtype=Tix.WINDOW,
                                        window=h.widget)
            self.bindWidget(h.widget)
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
                       BuiltIn("String"), BuiltIn("Empty"),
                       BIHex() ]
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

class RadioSel:
    def __init__(self, parent, radio):
        self.parent = parent
        self.radio = radio
        return

    def selected(self):
        self.parent.newradio(self.radio)
        return
    pass

class IsOk(Tix.DialogShell):
    def __init__(self, text, command):
        Tix.DialogShell.__init__(self, name="quit OK?")
        w = Tix.Label(self, text=text)
        w.pack(side=Tix.TOP, fill=Tix.BOTH, expand=1, )
        self.ok = Tix.Button(self, text='Ok', command=self.ok);
        self.ok.pack(side=Tix.LEFT, expand=1, padx=10, pady=8)
        self.cancel = Tix.Button(self, text='Cancel', command=self.cancel);
        self.cancel.pack(side=Tix.LEFT, expand=1, padx=10, pady=8)
        self.popup()
        self.command = command
        return

    def ok(self):
        self.popdown()
        self.command(True)
        return

    def cancel(self):
        self.destroy()
        self.command(False)
        return

    pass

class GUI(Tix.Frame):
    def __init__(self, filename=None, master=None):
        Tix.Frame.__init__(self, master)
        self.top = master
        self.pack(fill=Tix.BOTH, expand=1);
        master.geometry('800x400')
        self.createWidgets();
        self.filedialog = Tix.FileSelectDialog(master,
                                               command=self.open_select)
        self.saveasdialog = Tix.FileSelectDialog(master,
                                                 command=self.saveas_select)
        if (filename):
            self.openfile(filename)
            pass
        else:
            master.wm_title("yaesu edit")
            self.fd = None
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
        self.master.wm_title(filename + " (" + self.radioname + ")")
        for t in self.radio.toplevel:
            t.tab = self.tabs.add(t.name.lower(), label=t.name)
            t.setup(t.tab)
            pass
        pass
    
    def quitcmd(self, event=None):
        if (not self.fd or not self.fd.changed):
            self.quit()
            return
        IsOk("Unchanged data has not been written, do you"
             + " really want to quit?", self.quitok)
        pass

    def quitok(self, val):
        if (val):
            self.quit();
            pass
        return

    def opencmd(self, event=None):
        self.filedialog.popup()
        pass

    def open_select(self, filename):
        if (not filename):
            return
        if (not self.fd):
            self.openfile(filename)
        else:
            root = Tix.Tk()
            GUI(filename, root)
            pass
        return
    
    def saveascmd(self, event=None):
        if (not self.fd):
            return
        self.saveasdialog.popup()
        pass

    def saveas_select(self, filename):
        if (not filename):
            return
        self.fd.write(filename)
        return
    
    def savecmd(self, event=None):
        self.fd.write()
        pass

    def newradio(self, r):
        filename = radiodir + "/" + r.name + ".empty"
        self.open_select(filename)
        pass

    def createWidgets(self):
        self.buttons = Tix.Frame(self)
        self.buttons.pack(side=Tix.TOP, fill=Tix.X)
        
        self.filebutton = Tix.Menubutton(self.buttons, text="File",
                                         underline=0, takefocus=0)
        self.filemenu = Tix.Menu(self.filebutton, tearoff=0)
        self.filebutton["menu"] = self.filemenu
        m = Tix.Menu(self.filemenu)
        for r in radios:
            ns = RadioSel(self, r)
            m.add_command(label=r.name,
                          command = lambda ns=ns: ns.selected())
            pass
        self.filemenu.add_cascade(label="New", underline=1, menu=m)
        self.filemenu.add_command(label="Open", underline=1,
                                  accelerator="Ctrl+O",
                                  command = lambda self=self: self.opencmd() )
        self.filemenu.add_command(label="Save", underline=1,
                                  accelerator="Ctrl+S",
                                  command = lambda self=self: self.savecmd() )
        self.filemenu.add_command(label="Save As", underline=1,
                                  command = lambda self=self: self.saveascmd())
        self.filemenu.add_command(label="Exit", underline=1,
                                  accelerator="Ctrl+Q",
                                  command = lambda self=self: self.quitcmd() )
        self.top.bind_all("<Control-Q>", self.quitcmd)
        self.top.bind_all("<Control-q>", self.quitcmd)
        self.top.bind_all("<Control-S>", self.savecmd)
        self.top.bind_all("<Control-s>", self.savecmd)
        self.filebutton.pack(side=Tix.LEFT)

#         self.editbutton = Tix.Menubutton(self.buttons, text="Edit",
#                                          underline=0, takefocus=0)
#         self.editmenu = Tix.Menu(self.editbutton, tearoff=0)
#         self.editbutton["menu"] = self.editmenu
#         self.editmenu.add_command(label="Preferences", underline=1,
#                                   command = lambda self=self: self.opencmd() )
#         self.editbutton.pack(side=Tix.LEFT)

        self.tabs = Tix.NoteBook(self);
        self.tabs.pack(side=Tix.TOP, fill=Tix.BOTH, expand=1)

        pass

progname = sys.argv[0]

while len(sys.argv) > 1:
    if (sys.argv[1][0] != '-'):
        break
    if (sys.argv[1] == '--'):
        del sys.argv[0]
        break
    if (sys.argv[1] == '--configdir' or sys.argv[1] == '-f'):
        if (len(sys.argv[1]) < 2):
            print "No configuration directory given with " + sys.argv[1]
            sys.exit(1)
            pass
        del sys.argv[1]
        radiodir = sys.argv[1]
        del sys.argv[1]
    else:
        print "Unknown option: " + sys.argv[1]
        sys.exit(1)
        pass
    pass

if (len(sys.argv) > 1):
    filename = sys.argv[1]
else:
    filename = None
    pass

read_radios()

root = Tix.Tk();
gui = GUI(filename, root);
gui.mainloop();
sys.exit(0)
