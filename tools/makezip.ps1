param([string]$Source, [string]$Destination)
# Zip a folder with forward slashes in entry names (Compress-Archive in Windows PowerShell 5.1 writes backslashes).
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Source = (Resolve-Path $Source).Path.TrimEnd('\')
$parent = Split-Path $Source -Parent
if (Test-Path $Destination) { Remove-Item $Destination -Force }
$zip = [IO.Compression.ZipFile]::Open($Destination, 'Create')
try {
    Get-ChildItem $Source -Recurse -File | ForEach-Object {
        $name = $_.FullName.Substring($parent.Length + 1).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name, 'Optimal')
    }
} finally {
    $zip.Dispose()
}
