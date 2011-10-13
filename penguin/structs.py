import collections


itimerspec = collections.namedtuple("itimerspec", "it_value it_interval")
siginfo = collections.namedtuple("siginfo", "ssi_signum ssi_code")
