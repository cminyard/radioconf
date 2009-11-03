#!/usr/bin/env python

import Tix

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
