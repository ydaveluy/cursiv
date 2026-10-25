/* The tests side. ONE SERVER SERVES BOTH LANGUAGES, and OPENING THIS FOLDER is
   what makes the requirement names below resolve: the server scans it at
   `initialized`. This file alone, with no folder, reports every one of them
   unresolved -- which is the measurement in ../README.md. */
contact: "Donald Duck"

tst T001_good_case tests ReqId001_tstID, ReqId002_reqID
tst T002_environment testfile = "part2.tst" tests ReqId002_reqID applicable for Windows10
tst T003_coverage tests ReqId003_reqCov
