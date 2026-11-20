param([string]$Src, [string]$Out, [string]$Label)
$fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
$fsi.FileSystemsToCreate = 3   # ISO9660 + Joliet
$fsi.VolumeName = $Label
$fsi.Root.AddTree($Src, $false)
$img = $fsi.CreateResultImage()
$stream = $img.ImageStream
$cp = New-Object -TypeName System.CodeDom.Compiler.CompilerParameters
$cp.CompilerOptions = "/unsafe"
if (-not ("IsoWriter" -as [type])) {
Add-Type -CompilerParameters $cp -TypeDefinition @"
public class IsoWriter {
  public unsafe static void Write(string path, object stream, int blockSize, int blocks) {
    int bytes = 0; byte[] buf = new byte[blockSize]; var ptr = (System.IntPtr)(&bytes);
    var o = System.IO.File.OpenWrite(path);
    var i = stream as System.Runtime.InteropServices.ComTypes.IStream;
    while (blocks-- > 0) { i.Read(buf, blockSize, ptr); o.Write(buf, 0, bytes); }
    o.Flush(); o.Close();
  }
}
"@
}
[IsoWriter]::Write($Out, $stream, $img.BlockSize, $img.TotalBlocks)
