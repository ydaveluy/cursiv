/* The tests side. ONE SERVER SERVES BOTH LANGUAGES, and OPENING THIS FOLDER is
   what makes the requirement names below resolve: the server scans it at
   `initialized`. This file alone, with no folder, reports every one of them
   unresolved -- which is the measurement in ../README.md. */
contact: "Donald Duck"

/** Checks the naming rules. */
tst T001_good_case tests ReqId001_tstID, ReqId002_reqID
/** Checks a requirement on one environment. */
tst T002_environment testfile = "part2.tst" tests ReqId002_reqID applicable for Windows10
/** Checks the coverage rule. */
tst T003_coverage tests ReqId003_reqCov
