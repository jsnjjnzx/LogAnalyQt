#import <AppKit/AppKit.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>

static int fail(NSString *message) {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.messageText = @"配置测试版未能启动";
  alert.informativeText = message;
  [alert addButtonWithTitle:@"关闭"];
  [alert runModal];
  return 1;
}

int main(int argc, char **argv) {
  @autoreleasepool {
    NSBundle *bundle = NSBundle.mainBundle;
    NSString *previewRoot = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/LogAnalyQt Config Preview"];
    bool smoke = false;
    for (int i = 1; i < argc; ++i) smoke |= strcmp(argv[i], "--package-smoke") == 0;
    if (smoke) {
      NSString *requested = NSProcessInfo.processInfo.environment[@"LOGANALYQT_PREVIEW_ROOT"];
      NSString *resolved = requested.stringByResolvingSymlinksInPath;
      NSString *temporary = NSTemporaryDirectory().stringByResolvingSymlinksInPath;
      if (!resolved || (![resolved hasPrefix:[temporary stringByAppendingString:@"/"]] && ![resolved hasPrefix:@"/private/tmp/"]))
        return fail(@"包验证必须使用临时目录。");
      previewRoot = resolved;
    }
    NSString *home = [previewRoot stringByAppendingPathComponent:@"home"];
    NSString *data = [home stringByAppendingPathComponent:@"Library/Application Support/LogAnalyQt"];
    NSString *runtime = [bundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"appLogAnalyQt"];
    NSFileManager *files = NSFileManager.defaultManager;
    if (argc == 2 && strcmp(argv[1], "--preview-info") == 0) {
      NSData *info = [NSJSONSerialization dataWithJSONObject:@{ @"testHome": home, @"dataRoot": data, @"runtime": runtime, @"credentialNamespace": @"LogAnalyQt.ConfigPreview", @"automaticUpdates": @NO } options:0 error:nil];
      fwrite(info.bytes, 1, info.length, stdout); fputc('\n', stdout);
      return 0;
    }
    NSError *error = nil;
    if (![files fileExistsAtPath:data]) {
      if (![files createDirectoryAtPath:data.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error] ||
          ![files copyItemAtPath:[bundle.resourcePath stringByAppendingPathComponent:@"DemoData"] toPath:data error:&error])
        return fail(error.localizedDescription ?: @"无法准备独立的演示配置。");
    }
    setenv("LOGANALYQT_TEST_HOME", home.fileSystemRepresentation, 1);
    setenv("LOGANALYQT_CONFIG_PREVIEW", "1", 1);
    unsetenv("LOGANALYQT_UPDATE_TEST_RESULT");
    // Only harmless rendering/capture arguments are forwarded for package smoke checks.
    std::vector<char *> arguments{const_cast<char *>(runtime.fileSystemRepresentation), const_cast<char *>("--ui-smoke")};
    bool capture = false;
    for (int i = 1; i < argc; ++i) {
      if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) { capture = true; arguments.push_back(argv[i]); arguments.push_back(argv[++i]); }
      else if (strcmp(argv[i], "--personal-config") == 0) arguments.push_back(argv[i]);
      else if (strcmp(argv[i], "--migration-screenshot") == 0 && i + 1 < argc) { arguments.push_back(argv[i]); arguments.push_back(argv[++i]); }
    }
    if (smoke) {
      for (int i = 1; i + 1 < argc; ++i) if (strcmp(argv[i], "--migration-action") == 0) { arguments.push_back(argv[i]); arguments.push_back(argv[++i]); }
    }
    if (!capture && !smoke) arguments.push_back(const_cast<char *>("--interactive"));
    arguments.push_back(nullptr);
    execv(runtime.fileSystemRepresentation, arguments.data());
    return fail(@"无法启动测试程序，请保留完整的 .app 后重试。");
  }
}
