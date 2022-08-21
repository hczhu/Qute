# Qute

Qute is a C++ library for a lightweight search query execution engine. Qute supports [Qute query language](https://github.com/hczhu/Qute#qute-query-language). 
<br>See the code snippet below for a simple example of using Qute. The full compilable code is at [examples/full_text_search.cc](https://github.com/hczhu/Qute/blob/main/examples/full_text_search.cc)

```c++
const std::vector<std::string> kMarkTwainQuotes = {
    "A man is never more truthful than when he acknowledges himself a liar.",
    "I don't give a damn for a man that can only spell a word one way.",
    "The human race has one really effective weapon, and that is laughter.",
    "Loyalty to petrified opinion never yet broke a chain or freed a human soul.",
};

...

int main() {
  IteratorFactory iteratorFactory;
  qute::QueryParser queryParser(iteratorFactory);
  // Search for quotes having
  //     "man" and "liar"
  //   or having "human" but without "weapon".
  const std::string query = R"(
    (or (and tag:man_liar man liar )
        (diff tag:human-weapon human weapon)
    )
  )";
  auto itr = queryParser.getIterator(query);
  std::cout << "Search results:" << std::endl;
  for (; itr->valid(); itr->next()) {
    std::cout << "  " << kMarkTwainQuotes[itr->value()];
    auto tags = itr->getTags();
    if (!tags.empty()) {
      std::cout << " (" << tags[0] << ")";
    }
    std::cout << std::endl;
  }
  // Output
  /* 
   * Search results:
   *   A man is never more truthful than when he acknowledges himself a liar. (man_liar)
   *   Loyalty to petrified opinion never yet broke a chain or freed a human soul. (human-weapon)
   * 
   */
  return 0;
}

```

## Qute query language

Qute query language is a [Context-free Language](https://en.wikipedia.org/wiki/Context-free_language) following the following grammar 

```
Query :-> Term | (Operator Query_list) | (Operator Query_list) | (Operator Query Query)

Operator :-> Operator_without_tag | (Operator_without_tag tag:Token)

Operator_without_tag :-> and | or | diff

Query_list :-> Query Query_list | Term

Term :-> Token

Token :-> any string consisting of no whitespace, '(' or ')'.

```
The following one is a fairly complex Qute query example
```
(diff
    ( or tag:or (and tag:meta facebook c:facebook)
         (and tag:goog google c:google)
         (or tag:aapl apple)
    )
    nothing
)
```

## How to use Qute library

Qute can be easily imported to any C++ project built by [Bazel](https://bazel.build/).
<br> Add the following content to your Bazel `WORKSPACE` file.
```
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

git_repository(
    name = "com_github_tickertick_qute",
    commit = "58c152688c4958605c657cf3e40d1aba3fee908a",
    remote = "https://github.com/hczhu/Qute.git",
)

http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
    strip_prefix = "gflags-2.2.2",
    urls = ["https://github.com/gflags/gflags/archive/v2.2.2.tar.gz"],
)

http_archive(
    name = "com_github_glog",
    sha256 = "8a83bf982f37bb70825df71a9709fa90ea9f4447fb3c099e1d720a439d88bad6",
    strip_prefix = "glog-0.6.0",
    urls = ["https://github.com/google/glog/archive/refs/tags/v0.6.0.tar.gz"],
)
```
To depend on Qute, add the following dependancy to your Bazel target.
```
    deps = [
        "@com_github_tickertick_qute//:qute",
    ],
```
Also specify compiler option `-std=c++2a` in either Bazel command line or in the `.bazelrc` file.
```
build --cxxopt='-std=c++2a'
```
See [an example C++ project depending on Qute](https://github.com/hczhu/Qute-examples)

## Build

Qute is built and tested on Linux. Qute uses [Bazel](https://bazel.build/) to build.
Run the following commands in a Linux Shell to build Qute library.

```bash 
git clone https://github.com/hczhu/Qute.git
cd Qute
./install-bazel.sh
bazel build :qute
```

Run Qute tests
```bash 
bazel test test/...
```

Run the examples
```bash 
bazel run examples/...
```
