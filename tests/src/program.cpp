#include "program.hpp" // The header you're testing

#include <doctest/doctest.h>
#include <iostream>

std::vector<std::string> urls{
  "https://example.com/",
  "https://www.wikipedia.org/",
  "https://news.ycombinator.com/",
  "https://github.com/",
  "https://www.nytimes.com/",
  "https://blog.cloudflare.com/",
  "https://www.bbc.com/news",
  "https://api.github.com/repos/rebelot/heirline.nvim",
  "https://www.reddit.com/r/programming/",
};

// could run faster, but I will keep it like this for now.
TEST_CASE("request_html")
{
  Program p{};

  for(auto& url : urls) {
    auto [final_url, content] = p.request_html(url);

    lxb_html_document_t* doc = lxb_html_document_create();
    REQUIRE(doc != nullptr); // sanity check

    lxb_status_t status = lxb_html_document_parse(
      doc,
      reinterpret_cast<const lxb_char_t*>(content.c_str()),
      content.size());

    CHECK_MESSAGE(status == LXB_STATUS_OK, "Failed to request " << url);

    lxb_html_document_destroy(doc);
  }
}

TEST_CASE("parse_url")
{
  Program p{};

  int index = 0;
  for(auto& url : urls) {
    auto [final_url, content] = p.request_html(url);

    CHECK(!content.empty());

    std::unordered_set<Program::URL> nodes = p.parse_url(final_url, content);
    CHECK(!nodes.empty());
    CHECK(index != 0);
    std::cout << "for page " << url << ", here are the " << nodes.size() << " extracted links:" << std::endl;
    for(Program::URL const& child_url : nodes) {
      std::cout << child_url << std::endl;
      CHECK_MESSAGE(Program::is_valid_url(child_url), "Invalid URL extracted: " << child_url);
    }
  }
  std::cout << "test completed with " << index << "urls" << std::endl;
}

TEST_CASE("") {

}
