ml-gbdt
=====================

**ml-gbdt** is a GBDT(MART) and LambdaMART training and predicting package.

Compiling
---------
**make** it or compile it with **Visual Studio**.

Training
--------
>./gbdt-train -c [configuration file]

>./lm-train -c [configuration file]

Predicting
--------
>./gbdt-predict -c [configuration file]

>./lm-predict -c [configuration file]

Configuration File
------------------
###An Example for gbdt-train/gbdt-predict

>verbose = 1

>max_level = 5

>max_leaf_number = 20

>min_values_in_leaf = 10

>tree_number = 400

>learning_rate = 0.1

>training_sample = input

>training_sample_format = liblinear

>model = output.json

>gbdt_sample_rate = 0.9

>gbdt_loss = ls

###An Example for lm-train/lm-predict

>verbose = 1

>max_level = 5

>max_leaf_number = 20

>min_values_in_leaf = 10

>tree_number = 400

>learning_rate = 0.1

>training_sample = input

>training_sample_format = liblinear

>model = output.json

>lm_metric = ndcg

>lm_ndcg_k = 5

###Specification

All fields and values are case-sensitive.

####verbose
0, print least information
1, print extra information

####max_level
Max level of all decision trees.

####max_leaf_number
Max number of leaf node in all decision trees.

####min_values_in_leaf
It should be >= 1.

**ml-gbdt** will stop splitting a node when it has less equal than **min_values_in_leaf** training samples, and make it a leaf node.

####tree_number
Number of trees.

####learning_rate
Learning rate, should be in [0.0, 1.0], defined at **Friedman (March 1999)**.

####training_sample
File name of training samples.

####training_sample_format
Training sample format, can be "liblinear" or "gbdt".

**gbdt-train/gbdt-predict** is fully compatible with [liblinear](http://www.csie.ntu.edu.tw/~cjlin/liblinear/)/[libsvm](http://www.csie.ntu.edu.tw/~cjlin/libsvm/) format. An example is:

>+1 1:0.708333 2:1 3:1 4:-0.320755 5:-0.105023 6:-1 7:1 8:-0.419847 9:-1 10:-0.225806 12:1 13:-1

>-1 1:0.583333 2:-1 3:0.333333 4:-0.603774 5:1 6:-1 7:1 8:0.358779 9:-1 10:-0.483871 12:-1 13:1

>+1 1:0.166667 2:1 3:-0.333333 4:-0.433962 5:-0.383562 6:-1 7:-1 8:0.0687023 9:-1 10:-0.903226 11:-1 12:-1 13:1

While I have defined another format for some reasons below.

An example of gbdt format is:

> \#n c n n n n n n n n

> 0 61 0 60 468 36 0 52 1 1 0

> 0 57 1 233 145 5 0 107 20 2 0

> 1 w:5.5 53 0 313 6 0 0 4 0 2 0

> 1 w:4 33 0 1793 341 18 0 181 0 0 0



> **Some Explanations:**

> The first line shows there are 10 features, the 2nd of which is a category feature, others are numerical features.

> Values of category features must be integers.

> Values of numerical features and "y" can be double floats or integers(treated as double float internally).

> "y" can be 0/1 to model a binary classification problem(**NOTE**: liblinear is -1/1), or any real numbers for regression.

> From the 2nd line on, the 1st column is the "y" values, others are ordered "x" values.

> The 4th and 5th line contains "w:5.5", "w:4" respectively. 5.5 and 4 are weights of the two training samples.
> Default weights are 1.0.

> **Advantages:**

> Category features.

> Feature weights.

**lm-train/lm-predict** don't use it now, because only a [LECTOR 4.0](http://research.microsoft.com/en-us/um/beijing/projects/letor//letor4dataset.aspx) format is supported.

But keep it for future usage.

An example of LECTOR 4.0 format is:

> 2 qid:10032 1:0.056537 2:0.000000 3:0.666667 4:1.000000 5:0.067138 ... 45:0.000000 46:0.076923 #docid = GX029-35-5894638 inc = 0.0119881192468859 prob = 0.139842

> 0 qid:10032 1:0.279152 2:0.000000 3:0.000000 4:0.000000 5:0.279152 ... 45:0.250000 46:1.000000 #docid = GX030-77-6315042 inc = 1 prob = 0.341364 

> 0 qid:10032 1:0.130742 2:0.000000 3:0.333333 4:0.000000 5:0.134276 ... 45:0.750000 46:1.000000 #docid = GX140-98-13566007 inc = 1 prob = 0.0701303

> 1 qid:10032 1:0.593640 2:1.000000 3:0.000000 4:0.000000 5:0.600707 ... 45:0.500000 46:0.000000 #docid = GX256-43-0740276 inc = 0.0136292023050293 prob = 0.400738

####model
File name of the model, the output for "gbdt-train/lm-train" and the input for "gbdt-predict/lm-predict".
It is in json and very easy to understand.

####gbdt_sample_rate
GBDT Sample rate, should be in [0.0, 1.0], defined at **Friedman (March 1999)**.

**lm-train/lm-predict ignores it.**

####gbdt_loss
GBDT loss type, can be "ls", "lad" or "logistic".

LS/LAD loss is suitable for 0/1 or -1/1 classification and regression.

Logistic loss is only suitable for -1/1 binary classification.

LS, LAD and logistic loss are defined at **Friedman (February 1999)**

**lm-train/lm-predict ignores it.**

####lm_metric
LambdaMART metric, can be "ndcg".

**gbdt-train/gbdt-predict ignores it.**

####lm_ndcg_k
When lm_metric is "ndcg", ndcg@k is the real metric used, where k=lm_ndcg_k.

**gbdt-train/gbdt-predict ignores it.**

Others
-----
### json2cxx.py
"json2cxx.py" lies in directory "bin".
It can be used to convert a model(json) to a c++ predicting function, so that an interpreter for predicting is avoided.


Reference
---------
[Friedman, J. H. "Greedy Function Approximation: A Gradient Boosting Machine." (February 1999)](http://www-stat.stanford.edu/~jhf/ftp/trebst.pdf)

[Friedman, J. H. "Stochastic Gradient Boosting." (March 1999)](https://statweb.stanford.edu/~jhf/ftp/stobst.pdf)

[Qiang Wu, Christopher J. C. Burges, etc. "Adapting Boosting for Information Retrieval Measures" (2009)](http://research.microsoft.com/en-us/um/people/cburges/papers/lambdamart.pdf)

[Christopher J.C. Burges. "From RankNet to LambdaRank to LambdaMART: An Overview" (2010)](http://research.microsoft.com/pubs/132652/MSR-TR-2010-82.pdf)

