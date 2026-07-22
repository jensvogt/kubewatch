function Component() {}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {

        var targetExe = installer.value("TargetDir") + "/kubewatch.exe";

        var startMenuDir =
            installer.value("StartMenuDir") + "/KubeWatch";

        component.addOperation(
            "CreateShortcut",
            targetExe,
            startMenuDir + "/kubewatch.lnk",
            "workingDirectory=" + installer.value("TargetDir"),
            "iconPath=" + targetExe,
            "description=KubeWatch Kubernetes cluster viewer"
        );

        component.addOperation(
            "CreateShortcut",
            targetExe,
            installer.value("DesktopDir") + "/kubewatch.lnk",
            "workingDirectory=" + installer.value("TargetDir")
        );
    }
};
