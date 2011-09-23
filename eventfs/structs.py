import collections


itimerspec = collections.namedtuple("itimerspec", "it_value it_interval")
signalfd_siginfo = collections.namedtuple(
        "signalfd_siginfo", "ssi_signum ssi_code")
