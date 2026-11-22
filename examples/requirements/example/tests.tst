// Each test names the requirements it covers in requirements.req.
contact: "Donald Duck"

/** Checks the naming rules. */
tst T001_good_case tests ReqId001_tstID, ReqId002_reqID
/** Checks a requirement on one environment. */
tst T002_environment testfile = "part2.tst" tests ReqId002_reqID applicable for Windows10
/** Checks the coverage rule. */
tst T003_coverage tests ReqId003_reqCov
