"""
<name>Linear Projection</name>
<description>Create a linear projection.</description>
<contact>Gregor Leban (gregor.leban@fri.uni-lj.si)</contact>
<icon>icons/LinProj.png</icon>
<priority>2000</priority>
"""
# LinProj.py
#
# Show a linear projection of the data
# 

from OWWidget import *
from random import betavariate 
from OWLinProjGraph import *
from OWkNNOptimization import OWVizRank
from OWClusterOptimization import *
from OWFreeVizOptimization import *
import time
import OWToolbars, OWGUI, orngTest, orangeom
import OWVisFuncts, OWDlgs
import orngVizRank

###########################################################################################
##### WIDGET : Linear Projection
###########################################################################################
class OWLinProj(OWWidget):
    settingsList = ["graph.pointWidth", "graph.jitterSize", "graph.globalValueScaling", "graph.showFilledSymbols", "graph.scaleFactor",
                    "graph.showLegend", "graph.optimizedDrawing", "graph.useDifferentSymbols", "autoSendSelection", "graph.useDifferentColors",
                    "graph.tooltipKind", "graph.tooltipValue", "toolbarSelection", "graph.showClusters", "VizRankClassifierName", "clusterClassifierName",
                    "showOptimizationSteps", "valueScalingType", "graph.showProbabilities", "showAllAttributes",
                    "learnerIndex", "colorSettings", "addProjectedPositions"]
    jitterSizeNums = [0.0, 0.01, 0.1, 0.5, 1, 2, 3, 4, 5, 7, 10, 15, 20]
    jitterSizeList = [str(x) for x in jitterSizeNums]
    scaleFactorNums = [1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.2, 2.4, 2.6, 2.8, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0]

    contextHandlers = {"": DomainContextHandler("", [ContextField("shownAttributes", DomainContextHandler.RequiredList, selected="selectedShown", reservoir="hiddenAttributes")])}
        
    def __init__(self,parent=None, signalManager = None, name = "Linear Projection"):
        OWWidget.__init__(self, parent, signalManager, name, TRUE)

        self.inputs = [("Classified Examples", ExampleTableWithClass, self.cdata, Default), ("Example Subset", ExampleTable, self.subsetdata), ("Attribute Selection List", AttributeList, self.attributeSelection), ("Evaluation Results", orngTest.ExperimentResults, self.test_results), ("VizRank Learner", orange.Learner, self.vizRankLearner)]
        self.outputs = [("Selected Examples", ExampleTableWithClass), ("Unselected Examples", ExampleTableWithClass), ("Attribute Selection List", AttributeList), ("Learner", orange.Learner)]

        # local variables
        self.learnersArray = [None, None, None, None]   # VizRank, Cluster, FreeViz, S2N Heuristic Learner
        self.showAllAttributes = 0
        self.showOptimizationSteps = 0
        self.valueScalingType = 0
        self.autoSendSelection = 1
        self.data = None
        self.toolbarSelection = 0
        self.VizRankClassifierName = "VizRank classifier (Linear Projection)"
        self.clusterClassifierName = "Visual cluster classifier (Linear Projection)"
        self.classificationResults = None
        self.outlierValues = None
        self.attributeSelectionList = None
        self.learnerIndex = 0
        self.colorSettings = None
        self.addProjectedPositions = 0

        self.shownAttributes = []
        self.selectedShown = []
        self.hiddenAttributes = []
        self.selectedHidden = []

        #add a graph widget
        self.box = QVBoxLayout(self.mainArea)
        self.graph = OWLinProjGraph(self, self.mainArea, name)
        self.box.addWidget(self.graph)

        # cluster dialog
        self.clusterDlg = ClusterOptimization(self, self.signalManager, self.graph, name)
        self.graph.clusterOptimization = self.clusterDlg

        # freeviz dialog
        self.freeVizDlg = FreeVizOptimization(self, self.signalManager, self.graph, name)    
        
        # optimization dialog
        if name.lower() == "radviz": self.optimizationDlg = OWVizRank(self, self.signalManager, self.graph, orngVizRank.RADVIZ, name)
        else:                        self.optimizationDlg = OWVizRank(self, self.signalManager, self.graph, orngVizRank.LINEAR_PROJECTION, name)

        self.learnersArray[0] = VizRankLearner(RADVIZ, self.optimizationDlg, self.graph)
        self.learnersArray[2] = FreeVizLearner(self.freeVizDlg)

        # graph variables
        self.graph.manualPositioning = 0
        self.graph.hideRadius = 0
        self.graph.showClusters = 0
        self.graph.showAnchors = 1
        self.graph.jitterContinuous = 0
        self.graph.showProbabilities = 0
        self.graph.optimizedDrawing = 1
        self.graph.useDifferentSymbols = 0
        self.graph.useDifferentColors = 1
        self.graph.tooltipKind = 0
        self.graph.tooltipValue = 0
        self.graph.scaleFactor = 1.0
        
        #load settings
        self.loadSettings()
        self.graph.normalizeExamples = (name.lower() == "radviz")       # ignore settings!! if we have radviz then normalize, otherwise not.

        #GUI
        # add a settings dialog and initialize its values
        self.tabs = QTabWidget(self.space, 'tabWidget')
        self.GeneralTab = QVGroupBox(self)
        #self.GeneralTab.setFrameShape(QFrame.NoFrame)
        self.SettingsTab = QVGroupBox(self)
        self.tabs.insertTab(self.GeneralTab, "General")
        self.tabs.insertTab(self.SettingsTab, "Settings")
 
        #add controls to self.controlArea widget
        self.shownAttribsGroup = OWGUI.widgetBox(self.GeneralTab, " Shown Attributes " )
        hbox = OWGUI.widgetBox(self.shownAttribsGroup, orientation = 'horizontal')
        self.addRemoveGroup = OWGUI.widgetBox(self.GeneralTab, 1, orientation = "horizontal" )
        self.hiddenAttribsGroup = OWGUI.widgetBox(self.GeneralTab, " Hidden Attributes ")
        self.optimizationButtons = OWGUI.widgetBox(self.GeneralTab, " Optimization Dialogs ", orientation = "horizontal")
        
        self.shownAttribsLB = OWGUI.listBox(hbox, self, "selectedShown", "shownAttributes", callback = self.resetAttrManipulation, selectionMode = QListBox.Extended)
        self.hiddenAttribsLB = OWGUI.listBox(self.hiddenAttribsGroup, self, "selectedHidden", "hiddenAttributes", callback = self.resetAttrManipulation, selectionMode = QListBox.Extended)

        self.optimizationDlgButton = OWGUI.button(self.optimizationButtons, self, "VizRank", callback = self.optimizationDlg.reshow, tooltip = "Opens VizRank dialog, where you can search for interesting projections with different subsets of attributes.")
        self.clusterDetectionDlgButton = OWGUI.button(self.optimizationButtons, self, "Cluster", callback = self.clusterDlg.reshow)
        self.freeVizDlgButton = OWGUI.button(self.optimizationButtons, self, "FreeViz", callback = self.freeVizDlg.reshow, tooltip = "Opens FreeViz dialog, where the position of attribute anchors is optimized so that class separation is improved")
        self.optimizationDlgButton.setMaximumWidth(63)
        self.clusterDetectionDlgButton.setMaximumWidth(63)
        self.freeVizDlgButton.setMaximumWidth(63)
        
        self.connect(self.clusterDlg.startOptimizationButton , SIGNAL("clicked()"), self.optimizeClusters)
        self.connect(self.clusterDlg.resultList, SIGNAL("selectionChanged()"),self.showSelectedCluster)
        
        self.zoomSelectToolbar = OWToolbars.ZoomSelectToolbar(self, self.GeneralTab, self.graph, self.autoSendSelection)
        self.graph.autoSendSelectionCallback = self.selectionChanged
        self.connect(self.zoomSelectToolbar.buttonSendSelections, SIGNAL("clicked()"), self.sendSelections)
                               
        vbox = OWGUI.widgetBox(hbox, orientation = 'vertical')
        self.buttonUPAttr   = OWGUI.button(vbox, self, "", callback = self.moveAttrUP, tooltip="Move selected attributes up")
        self.buttonDOWNAttr = OWGUI.button(vbox, self, "", callback = self.moveAttrDOWN, tooltip="Move selected attributes down")
        self.buttonUPAttr.setPixmap(QPixmap(os.path.join(self.widgetDir, r"icons\Dlg_up1.png")))
        self.buttonUPAttr.setSizePolicy(QSizePolicy(QSizePolicy.Fixed , QSizePolicy.Expanding))
        self.buttonUPAttr.setMaximumWidth(20)
        self.buttonDOWNAttr.setPixmap(QPixmap(os.path.join(self.widgetDir, r"icons\Dlg_down1.png")))
        self.buttonDOWNAttr.setSizePolicy(QSizePolicy(QSizePolicy.Fixed , QSizePolicy.Expanding))
        self.buttonDOWNAttr.setMaximumWidth(20)
        self.buttonUPAttr.setMaximumWidth(20)

        self.attrAddButton =    OWGUI.button(self.addRemoveGroup, self, "", callback = self.addAttribute, tooltip="Add (show) selected attributes")
        self.attrAddButton.setPixmap(QPixmap(os.path.join(self.widgetDir, r"icons\Dlg_up2.png")))
        self.attrRemoveButton = OWGUI.button(self.addRemoveGroup, self, "", callback = self.removeAttribute, tooltip="Remove (hide) selected attributes")
        self.attrRemoveButton.setPixmap(QPixmap(os.path.join(self.widgetDir, r"icons\Dlg_down2.png")))
        self.showAllCB = OWGUI.checkBox(self.addRemoveGroup, self, "showAllAttributes", "Show all", callback = self.cbShowAllAttributes) 

        # ####################################
        # SETTINGS TAB
        # #####
        OWGUI.hSlider(self.SettingsTab, self, 'graph.pointWidth', box=' Point Size ', minValue=1, maxValue=15, step=1, callback = self.updateGraph)

        box = OWGUI.widgetBox(self.SettingsTab, " Jittering Options ")
        OWGUI.comboBoxWithCaption(box, self, "graph.jitterSize", 'Jittering size (% of size)  ', callback = self.resetGraphData, items = self.jitterSizeNums, sendSelectedValue = 1, valueType = float)
        OWGUI.checkBox(box, self, 'graph.jitterContinuous', 'Jitter continuous attributes', callback = self.resetGraphData, tooltip = "Does jittering apply also on continuous attributes?")

        box2a = OWGUI.widgetBox(self.SettingsTab, self, " Scaling ")
        OWGUI.comboBoxWithCaption(box2a, self, "graph.scaleFactor", 'Scale point position by: ', callback = self.updateGraph, items = self.scaleFactorNums, sendSelectedValue = 1, valueType = float)
        OWGUI.comboBoxWithCaption(box2a, self, "valueScalingType", 'Scale values by: ', callback = self.setValueScaling, items = ["attribute range", "global range", "attribute variance"])

        box3 = OWGUI.widgetBox(self.SettingsTab, " General Graph Settings ")
        
        #OWGUI.checkBox(box3, self, 'graph.normalizeExamples', 'Normalize examples', callback = self.updateGraph)
        OWGUI.checkBox(box3, self, 'graph.showLegend', 'Show legend', callback = self.updateGraph)
        OWGUI.checkBox(box3, self, 'graph.optimizedDrawing', 'Optimize drawing', callback = self.updateGraph, tooltip = "Speed up drawing by drawing all point belonging to one class value at once")
        OWGUI.checkBox(box3, self, 'graph.useDifferentSymbols', 'Use different symbols', callback = self.updateGraph, tooltip = "Show different class values using different symbols")
        OWGUI.checkBox(box3, self, 'graph.useDifferentColors', 'Use different colors', callback = self.updateGraph, tooltip = "Show different class values using different colors")
        OWGUI.checkBox(box3, self, 'graph.showFilledSymbols', 'Show filled symbols', callback = self.updateGraph)
        OWGUI.checkBox(box3, self, 'graph.showClusters', 'Show clusters', callback = self.updateGraph, tooltip = "Show a line boundary around a significant cluster")
        OWGUI.checkBox(box3, self, 'graph.showProbabilities', 'Show probabilities', callback = self.updateGraph, tooltip = "Show a background image with class probabilities")

        # ####
        hbox = OWGUI.widgetBox(self.SettingsTab, "Colors", orientation = "horizontal")
        OWGUI.button(hbox, self, "Set Colors", self.setColors, tooltip = "Set the canvas background color and color palette for coloring continuous variables")
        
        box2 = OWGUI.widgetBox(self.SettingsTab, " Tooltips Settings ")
        OWGUI.comboBox(box2, self, "graph.tooltipKind", items = ["Show line tooltips", "Show visible attributes", "Show all attributes"], callback = self.updateGraph)
        OWGUI.comboBox(box2, self, "graph.tooltipValue", items = ["Tooltips show data values", "Tooltips show spring values"], callback = self.updateGraph, tooltip = "Do you wish that tooltips would show you original values of visualized attributes or the 'spring' values (values between 0 and 1). \nSpring values are scaled values that are used for determining the position of shown points. Observing these values will therefore enable you to \nunderstand why the points are placed where they are.")

        box4 = OWGUI.widgetBox(self.SettingsTab, " Sending Selection ")
        OWGUI.checkBox(box4, self, 'autoSendSelection', 'Auto send selected/unselected data', callback = self.selectionChanged, tooltip = "Send signals with selected data whenever the selection changes.")
        OWGUI.comboBox(box4, self, "addProjectedPositions", items = ["Do not modify the domain", "Append projection as attributes", "Append projection as meta attributes"], callback = self.sendSelections)
        self.selectionChanged()

        self.activeLearnerCombo = OWGUI.comboBox(self.SettingsTab, self, "learnerIndex", box = " Set Active Learner ", items = ["VizRank Learner", "Cluster Learner", "FreeViz Learner", "S2N Feature Selection Learner"], tooltip = "Select which of the possible learners do you want to send on the widget output.", callback = self.setActiveLearner)

        # ####################################
        self.connect(self.graphButton, SIGNAL("clicked()"), self.saveToFile)

        self.icons = self.createAttributeIconDict()

        # add a settings dialog and initialize its values
        self.activateLoadedSettings()
        self.setValueScaling() # XXX is there any better way to do this?!
        self.resize(900, 700)

    def saveToFile(self):
        self.graph.saveToFile([("Save PixTex", self.graph.savePicTeX)])

    def activateLoadedSettings(self):
        dlg = self.createColorDialog()
        self.colorPalette = dlg.getColorPalette("colorPalette")
        self.graph.setCanvasBackground(dlg.getColor("Canvas"))
                
        apply([self.zoomSelectToolbar.actionZooming, self.zoomSelectToolbar.actionRectangleSelection, self.zoomSelectToolbar.actionPolygonSelection][self.toolbarSelection], [])

        self.clusterDlg.changeLearnerName(self.clusterClassifierName)
        
        self.cbShowAllAttributes()
        self.setActiveLearner()
        

    # #########################
    # KNN OPTIMIZATION BUTTON EVENTS
    # #########################
    def saveCurrentProjection(self):
        qname = QFileDialog.getSaveFileName( os.path.realpath(".") + "/Linear_projection.tab", "Orange Example Table (*.tab)", self, "", "Save File")
        if qname.isEmpty(): return
        name = str(qname)
        if len(name) < 4 or name[-4] != ".":
            name = name + ".tab"
        self.graph.saveProjectionAsTabData(name, self.getShownAttributeList())


    # ################################################################################################
    # find projections that have tight clusters of points that belong to the same class value
    def optimizeClusters(self):
        if self.data == None: return
        if not self.data.domain.classVar or not self.data.domain.classVar.varType == orange.VarTypes.Discrete:
            QMessageBox.critical( None, "Cluster Detection Dialog", 'Clusters can be detected only in data sets with a discrete class value', QMessageBox.Ok)
            return

        self.clusterDlg.clearResults()
        self.clusterDlg.clusterStabilityButton.setOn(0)
        self.clusterDlg.pointStability = None

        try:
            listOfAttributes = self.optimizationDlg.getEvaluatedAttributes(self.data)
            text = str(self.optimizationDlg.attributeCountCombo.currentText())
            if text == "ALL": maxLen = len(listOfAttributes)
            else:             maxLen = int(text)
            
            if self.clusterDlg.getOptimizationType() == self.clusterDlg.EXACT_NUMBER_OF_ATTRS: minLen = maxLen
            else: minLen = 3
                        
            possibilities = 0
            for i in range(minLen, maxLen+1): possibilities += OWVisFuncts.combinationsCount(i, len(listOfAttributes))* OWVisFuncts.fact(i-1)/2
                
            self.graph.totalPossibilities = possibilities
            self.graph.triedPossibilities = 0
        
            if self.graph.totalPossibilities > 20000:
                proj = str(self.graph.totalPossibilities)
                l = len(proj)
                for i in range(len(proj)-2, 0, -1):
                    if (l-i)%3 == 0: proj = proj[:i] + "," + proj[i:]
                self.printVerbose("OWLinProj: Warning: There are %s possible projections using currently visualized attributes"% (proj))
            
            self.clusterDlg.disableControls()
            
            self.graph.getOptimalClusters(listOfAttributes, minLen, maxLen, self.clusterDlg.addResult)
        except:
            type, val, traceback = sys.exc_info()
            sys.excepthook(type, val, traceback)  # print the exception

        self.clusterDlg.enableControls()
        self.clusterDlg.finishedAddingResults()
        self.showSelectedCluster()
   

    # send signals with selected and unselected examples as two datasets
    def sendSelections(self):
        if not self.data: return
        #(selected, unselected, merged) = self.graph.getSelectionsAsExampleTables(self.getShownAttributeList())
        (selected, unselected) = self.graph.getSelectionsAsExampleTables(self.getShownAttributeList(), addProjectedPositions = self.addProjectedPositions)
    
        self.send("Selected Examples",selected)
        self.send("Unselected Examples",unselected)
        #self.send("Example Distribution", merged)

    def sendShownAttributes(self):
        self.send("Attribute Selection List", [a[0] for a in self.shownAttributes])


    # show selected interesting projection
    def showSelectedAttributes(self):
        val = self.optimizationDlg.getSelectedProjection()
        if val:
            (accuracy, other_results, tableLen, attrList, tryIndex, generalDict) = val
            self.updateGraph(attrList, setAnchors= 1, XAnchors = generalDict.get("XAnchors"), YAnchors = generalDict.get("YAnchors"))
            self.graph.removeAllSelections()


    def showSelectedCluster(self):
        val = self.clusterDlg.getSelectedCluster()
        if not val: return
        (value, closure, vertices, attrList, classValue, enlargedClosure, other, strList) = val

        if self.clusterDlg.clusterStabilityButton.isOn():
            validData = self.graph.getValidList([self.graph.attributeNames.index(attr) for attr in attrList])
            insideColors = (Numeric.compress(validData, self.clusterDlg.pointStability), "Point inside a cluster in %.2f%%")
        else: insideColors = None
        
        self.updateGraph(attrList, 1, insideColors, clusterClosure = (closure, enlargedClosure, classValue))
        self.graph.removeAllSelections()


    def getShownAttributeList(self):
        return [a[0] for a in self.shownAttributes]        

    def setShownAttributeList(self, data, shownAttributes = None):
        shown = []
        hidden = []

        if data:
            if shownAttributes:
                if type(self.shownAttributes[0]) == tuple:
                    shown = shownAttributes
                else:
                    shown = [(a.name, a.varType) for a in shownAttributes]
                hidden = filter(lambda x:x not in shown, [(a.name, a.varType) for a in data.domain.attributes])
            else:
                shown = [(a.name, a.varType) for a in data.domain.attributes]
                if not self.showAllAttributes:
                    hidden = shown[10:]
                    shown = shown[:10]

            if data.domain.classVar:
                hidden += [(data.domain.classVar.name, data.domain.classVar.varType)]

        self.shownAttributes = shown
        self.hiddenAttributes = hidden
        self.selectedHidden = []
        self.selectedShown = []
        self.resetAttrManipulation()

        self.sendShownAttributes()
    
    def updateGraph(self, attrList = None, setAnchors = 0, insideColors = None, clusterClosure = None, **args):
        if not attrList:
            attrList = self.getShownAttributeList()
        else:
            self.setShownAttributeList(self.data, attrList)
        
        if self.optimizationDlg.showKNNCorrectButton.isOn() or self.optimizationDlg.showKNNWrongButton.isOn():
            shortData = self.graph.createProjectionAsExampleTable([self.graph.attributeNameIndex[attr] for attr in attrList], settingsDict = {"useAnchorData": 1})
            kNNExampleAccuracy, probabilities = self.optimizationDlg.kNNClassifyData(shortData)
            if self.optimizationDlg.showKNNCorrectButton.isOn(): kNNExampleAccuracy = ([1.0 - val for val in kNNExampleAccuracy], "Probability of wrong classification = %.2f%%")
            else:   kNNExampleAccuracy = (kNNExampleAccuracy, "Probability of correct classification = %.2f%%")
        else:
            kNNExampleAccuracy = None

        self.graph.insideColors = insideColors or self.classificationResults or kNNExampleAccuracy or self.outlierValues
        self.graph.clusterClosure = clusterClosure

        self.graph.updateData(attrList, setAnchors, **args)
        self.graph.repaint()
        

    # ###############################################################################################################
    # INPUT SIGNALS
    
    # receive new data and update all fields
    def cdata(self, data, clearResults = 1):
        if data:
            name = getattr(data, "name", "")
            data = orange.Preprocessor_dropMissingClasses(data)
            data.name = name
            
        if self.data and data and self.data.checksum() == data.checksum():
            return    # check if the new data set is the same as the old one

        self.closeContext()        
        exData = self.data
        self.data = data
        self.graph.setData(self.data)
        self.optimizationDlg.setData(data)  
        self.clusterDlg.setData(data, clearResults)
        self.freeVizDlg.setData(data)
        self.graph.clusterClosure = None
        self.graph.insideColors = None
        
        reset = not (data and exData and str(exData.domain.attributes) == str(data.domain.attributes)) # preserve attribute choice if the domain is the same
        if reset:
            self.setShownAttributeList(self.data, self.attributeSelectionList)
            
        self.openContext("", data)
        self.resetAttrManipulation()
        self.updateGraph(setAnchors = reset)            
        self.sendSelections()

    def subsetdata(self, data, update = 1):
        if self.graph.subsetData != None and data != None and self.graph.subsetData.checksum() == data.checksum(): return    # check if the new data set is the same as the old one
        self.graph.subsetData = data
        if update: self.updateGraph()
        self.optimizationDlg.setSubsetData(data)
        self.clusterDlg.setSubsetData(data)
       

    # attribute selection signal - info about which attributes to show
    def attributeSelection(self, attributeSelectionList):
        self.attributeSelectionList = attributeSelectionList
        if self.data and self.attributeSelectionList:
            for attr in self.attributeSelectionList:
                if not self.graph.attributeNameIndex.has_key(attr):  # this attribute list belongs to a new dataset that has not come yet
                    return

            self.setShownAttributeList(self.data, self.attributeSelectionList)
            self.selectionChanged()
    
        self.updateGraph(setAnchors = 1)

    # visualize the results of the classification
    def test_results(self, results):
        self.classificationResults = None
        if isinstance(results, orngTest.ExperimentResults) and len(results.results) > 0 and len(results.results[0].probabilities) > 0:
            self.classificationResults = [results.results[i].probabilities[0][results.results[i].actualClass] for i in range(len(results.results))]
            self.classificationResults = (self.classificationResults, "Probability of correct classificatioin = %.2f%%")
                
        self.updateGraph(setAnchors = 1)

    
    # set the learning method to be used in VizRank
    def vizRankLearner(self, learner):
        self.optimizationDlg.externalLearner = learner        
        

    # ###############################################################################################################
    # EVENTS

    def resetAttrManipulation(self):
        cannotMove = not self.shownAttributes or len(self.selectedShown) != 1
        self.buttonUPAttr.setDisabled(cannotMove or not self.selectedShown[0])
        self.buttonDOWNAttr.setDisabled(cannotMove or self.selectedShown[0] >= len(self.shownAttributes)-1)
        self.attrAddButton.setDisabled(not self.selectedHidden or self.showAllAttributes)
        self.attrRemoveButton.setDisabled(not self.selectedShown or self.showAllAttributes)
        if self.hiddenAttributes and self.hiddenAttributes[0][0]!=self.data.domain.classVar.name:
            self.showAllCB.setChecked(0)
        

    # move selected attribute in "Attribute Order" list one place up
    def moveAttrUP(self):
        self.graph.insideColors = None
        self.graph.clusterClosure = None

        selected = self.selectedShown[0]
        if selected:
            print self.selectedShown
            self.shownAttributes = self.shownAttributes[:selected-1] + [self.shownAttributes[selected], self.shownAttributes[selected-1]] + self.shownAttributes[selected+1:]
            self.selectedShown[0] -= 1
            print self.selectedShown

        self.sendShownAttributes()
        self.graph.potentialsBmp = None
        self.updateGraph(setAnchors = 1)
        self.graph.removeAllSelections()

    # move selected attribute in "Attribute Order" list one place down  
    def moveAttrDOWN(self):
        self.graph.insideColors = None; self.graph.clusterClosure = None

        selected = self.selectedShown[0]
        if selected < len(self.shownAttributes) - 1:
            self.shownAttributes = self.shownAttributes[:selected] + [self.shownAttributes[selected+1], self.shownAttributes[selected]] + self.shownAttributes[selected+2:]
            self.selectedShown[0] += 1

        self.sendShownAttributes()
        self.graph.potentialsBmp = None
        self.updateGraph(setAnchors = 1)
        self.graph.removeAllSelections()

    def cbShowAllAttributes(self):
        if self.showAllAttributes:
            self.addAttribute(True)
        self.resetAttrManipulation()

    def addAttribute(self, addAll = False):
        self.graph.insideColors = None
        self.graph.clusterClosure = None

        if addAll:
            if self.data and self.data.domain.classVar:
                self.shownAttributes = self.shownAttributes + self.hiddenAttributes[:-1]
                self.hiddenAttributes = [self.hiddenAttributes[-1]]
        else:
            self.setShownAttributeList(self.data, self.shownAttributes + [self.hiddenAttributes[i] for i in self.selectedHidden])
        self.selectedHidden = []
        self.selectedShown = []
        self.resetAttrManipulation()
                
        if self.graph.globalValueScaling == 1:
            self.graph.rescaleAttributesGlobaly(self.data, self.getShownAttributeList())

        self.sendShownAttributes()
        self.updateGraph(setAnchors = 1)
        self.graph.replot()
        self.graph.removeAllSelections()

    def removeAttribute(self):
        self.graph.insideColors = None
        self.graph.clusterClosure = None

        newShown = self.shownAttributes[:]
        self.selectedShown.sort(lambda x,y:-cmp(x, y))
        for i in self.selectedShown:
            del newShown[i]
        self.setShownAttributeList(self.data, newShown)
                
        if self.graph.globalValueScaling == 1:
            self.graph.rescaleAttributesGlobaly(self.data, self.getShownAttributeList())
        self.sendShownAttributes()
        self.updateGraph(setAnchors = 1)
        self.graph.replot()
        self.graph.removeAllSelections()


    def resetBmpUpdateValues(self):
        self.graph.potentialsBmp = None
        self.updateGraph()

    def setActiveLearner(self):
        self.send("Learner", self.learnersArray[self.learnerIndex])
        
    def setManualPosition(self):
        self.graph.manualPositioning = self.manualPositioningButton.isOn()
        
    def resetGraphData(self):
        self.graph.setData(self.data)
        self.updateGraph()
        
    def setValueScaling(self):
        self.graph.insideColors = self.graph.clusterClosure = None
        if self.valueScalingType == 0:
            self.graph.globalValueScaling = self.graph.scalingByVariance = 0
        elif self.valueScalingType == 1:
            self.graph.globalValueScaling = 1
            self.graph.scalingByVariance = 0
        else:
            self.graph.globalValueScaling = 0
            self.graph.scalingByVariance = 1
        self.graph.setData(self.data)
        self.graph.potentialsBmp = None
        self.updateGraph()
        

    def selectionChanged(self):
        self.zoomSelectToolbar.buttonSendSelections.setEnabled(not self.autoSendSelection)
        if self.autoSendSelection: self.sendSelections()

    def setColors(self):
        dlg = self.createColorDialog()
        if dlg.exec_loop():
            self.colorSettings = (dlg.getColorSchemas(), dlg.getCurrentSchemeIndex(), dlg.getCurrentState())
            self.colorPalette = dlg.getColorPalette("colorPalette")
            self.graph.setCanvasBackground(dlg.getColor("Canvas"))
            self.updateGraph()

    def createColorDialog(self):
        c = OWDlgs.ColorPalette(self, "Color Palette")
        c.createColorPalette("colorPalette", "Continuous variable palette")
        box = c.createBox("otherColors", "Other Colors")
        c.createColorButton(box, "Canvas", "Canvas color", Qt.white)
        box.addSpace(5)
        box.adjustSize()
        if self.colorSettings:
            c.setColorSchemas(self.colorSettings[0], self.colorSettings[1])
            c.setCurrentState(self.colorSettings[2])
        else:
            c.setColorSchemas()
        return c

    def getColorPalette(self):
        return self.colorPalette

    # ###############################################################################################################
    # functions used by OWClusterOptimization class
    def setMinimalGraphProperties(self):
        attrs = ["graph.pointWidth", "graph.showLegend", "graph.showClusters", "autoSendSelection"]
        self.oldSettings = dict([(attr, mygetattr(self, attr)) for attr in attrs])
        self.graph.pointWidth = 3
        self.graph.showLegend = 0
        self.graph.showClusters = 0
        self.autoSendSelection = 0
        self.graph.showAttributeNames = 0
        self.graph.setAxisScale(QwtPlot.xBottom, -1.05, 1.05, 1)
        self.graph.setAxisScale(QwtPlot.yLeft, -1.05, 1.05, 1)


    def restoreGraphProperties(self):
        if hasattr(self, "oldSettings"):
            for key in self.oldSettings:
                self.__setattr__(key, self.oldSettings[key])
        self.graph.showAttributeNames = 1
        self.graph.setAxisScale(QwtPlot.xBottom, -1.22, 1.22, 1)
        self.graph.setAxisScale(QwtPlot.yLeft, -1.13, 1.13, 1)

    def destroy(self, dw = 1, dsw = 1):
        self.clusterDlg.hide()
        self.optimizationDlg.hide()
        self.freeVizDlg.hide()
        OWWidget.destroy(self, dw, dsw)


#test widget appearance
if __name__=="__main__":
    a=QApplication(sys.argv)
    ow=OWLinProj()
    a.setMainWidget(ow)
    ow.show()
    ow.cdata(orange.ExampleTable("..\\..\\doc\\datasets\\zoo"))
    a.exec_loop()

    #save settings 
    ow.saveSettings()
