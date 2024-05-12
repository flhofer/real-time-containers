handle SIGINT noprint nostop pass
define hook-step
set scheduler-locking on
end
define hookpost-step
set scheduler-locking off
end
define hook-run
set scheduler-locking off
end
define hook-continue
set scheduler-locking off
end
define hookpost-run
set scheduler-locking step
end

