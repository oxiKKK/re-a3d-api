Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[System.Windows.Forms.Application]::EnableVisualStyles()
$state = @{ Remaining = 2 }
$onClosed = {
    $state.Remaining--
}.GetNewClosure()
$onKeyDown = {
    param($sender, $eventArgs)
    if ($eventArgs.Control -and $eventArgs.KeyCode -eq 'C') {
        $eventArgs.SuppressKeyPress = $true
        $state.Remaining = 0
    }
}.GetNewClosure()

$forms = @()
foreach ($title in @('Calculator', 'My Computer')) {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = $title
    $form.KeyPreview = $true
    $form.Add_KeyDown($onKeyDown)
    $form.ClientSize = New-Object System.Drawing.Size(360, 200)
    $form.StartPosition = 'Manual'
    $form.Location = New-Object System.Drawing.Point((100 + 400 * $forms.Count), 150)

    $label = New-Object System.Windows.Forms.Label
    $label.Text = 'Dummy window'
    $label.Dock = 'Fill'
    $label.TextAlign = 'MiddleCenter'
    $form.Controls.Add($label)
    $form.Add_FormClosed($onClosed)
    $forms += $form
}

try {
    foreach ($form in $forms) {
        $form.Show()
    }
    # Return to PowerShell regularly so console Ctrl+C can run the cleanup.
    while ($state.Remaining -gt 0) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 50
    }
}
finally {
    foreach ($form in $forms) {
        $form.Dispose()
    }
}
