script_name = "StyleNameFix Unicode check"
script_description = "Manual regression check for Chinese Automation dialog buttons"
script_author = "StyleNameFix contributors"
script_version = "1.0"

aegisub.register_macro(script_name, script_description, function()
    aegisub.dialog.display({
        {class = "label", label = "中文按钮检查：下方应显示 应用、筛选、清空筛选、查看全名、取消。", x = 0, y = 0, width = 1, height = 1}
    }, {"应用", "筛选", "清空筛选", "查看全名", "取消"}, {ok = "应用", cancel = "取消"})
end)
