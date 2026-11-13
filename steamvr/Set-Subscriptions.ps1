param(
    [ValidateSet(0,8)][int]$Rings = 8,
    [Parameter(Mandatory=$true)][string]$Ini
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Ini -PathType Leaf)) { throw 'Existing INI missing.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SubscriptionIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
    [DllImport("kernel32.dll", EntryPoint="WritePrivateProfileStringW", CharSet=CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool Flush(IntPtr section, IntPtr key, IntPtr value, string file);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern uint GetPrivateProfileInt(string section, string key, int defaultValue, string file);
}
'@
$iniPath = (Resolve-Path -LiteralPath $Ini).Path
if (-not [SubscriptionIni]::WritePrivateProfileString('render','subscription_rings',[string]$Rings,$iniPath)) {
    throw 'Could not write subscription setting.'
}
$null = [SubscriptionIni]::Flush([IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero,$iniPath)
if ([SubscriptionIni]::GetPrivateProfileInt('render','subscription_rings',999,$iniPath) -ne $Rings) {
    throw 'Setting readback did not match.'
}
Write-Host "Subscription rings set to $Rings. Applies automatically while in an offline game. 0 means native coverage."
