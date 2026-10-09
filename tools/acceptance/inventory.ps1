[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Windows.Forms
[PSCustomObject]@{
  SerialPorts = @([System.IO.Ports.SerialPort]::GetPortNames())
  Adapters = @(Get-NetAdapter | Select-Object Name, Status, LinkSpeed, InterfaceDescription)
  FreeBytesC = (Get-PSDrive C).Free
  Screens = @([System.Windows.Forms.Screen]::AllScreens | ForEach-Object { [PSCustomObject]@{Name=$_.DeviceName;Primary=$_.Primary;Bounds=$_.Bounds.ToString()} })
  WindowsSandboxExecutablePresent = (Test-Path "$env:WINDIR\System32\WindowsSandbox.exe")
  VirtualBoxCliPresent = [bool](Get-Command VBoxManage -ErrorAction SilentlyContinue)
  PhysicalMemoryBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
} | ConvertTo-Json -Depth 5
