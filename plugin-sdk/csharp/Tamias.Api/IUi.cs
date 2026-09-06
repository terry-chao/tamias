namespace Tamias.Api;

public interface IUi
{
    DialogResult ShowMessage(string title, string message, DialogButtons buttons = DialogButtons.Ok);
    string? PromptString(string title, string label, string defaultValue = "");
    double? PromptNumber(string title, string label, double defaultValue = 0, double? min = null, double? max = null);
    bool ShowForm(PromptForm form);
    string? OpenFile(string title, string filter);
    string? SaveFile(string title, string filter, string? defaultName = null);
}
