param([int]$Id = 21524, [string]$Out = "perf51.tsv")

# Working set, CPU share and thread count of the editor, once every two seconds.
# CPU is a percentage of one core's worth of time divided by the core count, so
# 100 means every core busy - the same number Task Manager shows.
$p = Get-Process -Id $Id -ErrorAction Stop
$cores = [Environment]::ProcessorCount
$prev = $p.TotalProcessorTime
$prevT = Get-Date
"time`tMB`tCPU%`tthreads" | Out-File -FilePath $Out -Encoding utf8
while (-not $p.HasExited) {
    Start-Sleep -Seconds 2
    $p.Refresh()
    if ($p.HasExited) { break }
    $now = Get-Date
    $span = ($now - $prevT).TotalMilliseconds
    $cpu = if ($span -gt 0) {
        ($p.TotalProcessorTime - $prev).TotalMilliseconds / $span * 100 / $cores
    } else { 0 }
    $line = "{0}`t{1}`t{2}`t{3}" -f $now.ToString('HH:mm:ss'),
        [int]($p.WorkingSet64 / 1MB), [math]::Round($cpu, 1), $p.Threads.Count
    $line | Out-File -FilePath $Out -Encoding utf8 -Append
    $prev = $p.TotalProcessorTime
    $prevT = $now
}
