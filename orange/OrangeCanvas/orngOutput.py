# Author: Gregor Leban (gregor.leban@fri.uni-lj.si)
# Description:
#     print system output and exceptions into a window. Enables copy/paste
#
from qt import *
import sys
import string
from time import localtime
import traceback
import os.path, os
import orngResources

TRUE  = 1
FALSE = 0 

class OutputWindow(QMainWindow):
    def __init__(self, canvasDlg, *args):
        apply(QMainWindow.__init__,(self,) + args)
        self.canvasDlg = canvasDlg

        self.textOutput = QTextView(self)
        self.textOutput.setFont(QFont('Courier New',10, QFont.Normal))
        self.setCentralWidget(self.textOutput)
        self.setCaption("Output Window")
        self.setIcon(QPixmap(orngResources.output))

        self.defaultExceptionHandler = sys.excepthook
        self.defaultSysOutHandler = sys.stdout
        self.focusOnCatchException = 1
        self.focusOnCatchOutput  = 0
        self.printOutput = 1
        self.printException = 1
        self.writeLogFile = 1

        self.logFile = open(os.path.join(canvasDlg.canvasDir, "outputLog.htm"), "w") # create the log file
        #self.printExtraOutput = 0
        
        #sys.excepthook = self.exceptionHandler
        #sys.stdout = self
        #self.textOutput.setText("")
        #self.setFocusPolicy(QWidget.NoFocus)
        
        self.resize(700,500)
        self.showNormal()

    def closeEvent(self,ce):
        #QMessageBox.information(self,'Orange Canvas','Output window is used to print output from canvas and widgets and therefore can not be closed.','Ok')
        wins = self.canvasDlg.workspace.getDocumentList()
        if wins != []:
            wins[0].setFocus()

    def focusInEvent(self, ev):
        self.canvasDlg.enableSave(1)

    def catchException(self, catch):
        if catch: sys.excepthook = self.exceptionHandler
        else:     sys.excepthook = self.defaultExceptionHandler

    def catchOutput(self, catch):
        if catch:    sys.stdout = self
        else:         sys.stdout = self.defaultSysOutHandler

    def setFocusOnException(self, focusOnCatchException):
        self.focusOnCatchException = focusOnCatchException
        
    def setFocusOnOutput(self, focusOnCatchOutput):
        self.focusOnCatchOutput = focusOnCatchOutput

    def printOutputInStatusBar(self, printOutput):
        self.printOutput = printOutput

    def printExceptionInStatusBar(self, printException):
        self.printException = printException

    def setWriteLogFile(self, write):
        self.writeLogFile = write

    def clear(self):
        self.textOutput.setText("")
    
    # print text produced by warning and error widget calls
    def widgetEvents(self, text):
        if text != None:
            self.write(str(text))
        self.canvasDlg.setStatusBarEvent(QString(text))

    # simple printing of text called by print calls
    def write(self, text):
        # is this some extra info for debuging
        #if len(text) > 7 and text[0:7] == "<extra>":
        #    if not self.printExtraOutput: return
        #    text = text[7:]
        if text == "\n": return

        if (text.count("<") != text.count(">")) or text.count("\n") > 0 or text.count("\t") > 0:
            text = text.replace("<", "[")    # since this is rich text control, we have to replace special characters
            text = text.replace(">", "]")
            text = text.replace("\n", "<br>")   # replace new line characters with <br> otherwise they don't get shown correctly in html output
            text = "<nobr>" + text + "</nobr>"  

        if self.focusOnCatchOutput:
            self.canvasDlg.menuItemShowOutputWindow()
            self.canvasDlg.workspace.cascade()    # cascade shown windows

        if self.writeLogFile:
            self.logFile.write(str(text) + "<br>\n")
            
        self.textOutput.append(str(text))
        self.textOutput.ensureVisible(0, self.textOutput.contentsHeight())
        if self.printOutput:
            self.canvasDlg.setStatusBarEvent(text)
        
    def writelines(self, lines):
        for line in lines:
            self.write(line)

    def flush(self):
        pass

    def keyReleaseEvent (self, event):
        if event.state() & Qt.ControlButton != 0 and event.ascii() == 3:    # user pressed CTRL+"C"
            self.textOutput.copy()

    def exceptionHandler(self, type, value, tracebackInfo):
        if self.focusOnCatchException:
            self.canvasDlg.menuItemShowOutputWindow()
            self.canvasDlg.workspace.cascade()    # cascade shown windows

        text = ""
        t = localtime()
        text += "<nobr>Unhandled exception of type <b>%s </b> occured at %d:%02d:%02d:</nobr><br><nobr>Traceback:</nobr><br>" % ( str(type) , t[3],t[4],t[5])

        if self.printException:
            self.canvasDlg.setStatusBarEvent("Unhandled exception of type %s occured at %d:%02d:%02d" % ( str(type) , t[3],t[4],t[5]))

        # TO DO:repair this code to shown full traceback. when 2 same errors occur, only the first one gets full traceback, the second one gets only 1 item
        
        list = traceback.extract_tb(tracebackInfo, 10)
        space = "&nbsp &nbsp "
        totalSpace = space
        for i in range(len(list)):
            (file, line, funct, code) = list[i]
            if code == None: continue
            (dir, filename) = os.path.split(file)
            text += "<nobr>" + totalSpace + "File: <u>" + filename + "</u>  in line %4d</nobr><br>" %(line)
            text += "<nobr>" + totalSpace + "<nobr>Function name: %s</nobr><br>" % (funct)
            if i == len(list)-1:
                text += "<nobr>" + totalSpace + "Code: <b>" + code + "</b></nobr><br>"
            else:
                text += "<nobr>" + totalSpace + "Code: " + code + "</nobr><br>"
                totalSpace += space
            
        text += "<nobr>" + totalSpace + "Exception type: <b>" + str(type) + "</b></nobr><br>"
        text += "<nobr>" + totalSpace + "Exception value: <b>" + str(value)+ "</b></nobr><br><hr>"
        self.textOutput.append(text)
        self.textOutput.ensureVisible(0, self.textOutput.contentsHeight())

        if self.writeLogFile:
            self.logFile.write(str(text) + "<br>")
