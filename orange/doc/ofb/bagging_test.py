# Author:      B Zupan
# Version:     1.0
# Description: Test for bagging as defined in bagging.py
# Category:    modelling
# Uses:        adult_sample.tab, bagging.py

import orange, orngTree, orngStat, orngTest, orngStat, bagging
data = orange.ExampleTable("../datasets/adult_sample")

tree = orngTree.TreeLearner(mForPrunning=10, minExamples=30)
tree.name = "tree"
baggedTree = bagging.Learner(learner=tree, t=5)

learners = [tree, baggedTree]

results = orngTest.crossValidation(learners, data, folds=5)
for i in range(len(learners)):
    print learners[i].name, orngStat.CA(results)[i]
