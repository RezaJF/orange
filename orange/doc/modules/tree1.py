# Author:      J Zabkar
# Version:     1.0
# Description: Demostration of use of orngTree module: prin out
#              a tree in text and dot format
# Category:    modelling
# Uses:        iris

import orange, orngTree
data = orange.ExampleTable("iris.tab")
tree = orange.TreeLearner(data)
orngTree.printTxt(tree)
orngTree.printDot(tree, fileName="tree1.dot")
