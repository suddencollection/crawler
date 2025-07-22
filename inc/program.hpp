#pragma once

#include <string>
#include <vector>
//
#include <lexbor/dom/interfaces/node.h>

class PageNode
{
public:
  PageNode(std::string url) :
    url{url}, links{}
  {
  }

  ~PageNode()
  {
  }

  void add(PageNode node) { links.push_back(node); }
  void reserve(std::size_t size) { links.reserve(size); };
  auto children() -> std::vector<PageNode> const& { return links; }

private:
  std::string url;
  std::vector<PageNode> links;
};

class Program
{
  using ActualUrl_Content_Pair = std::pair<std::string, std::string>;
public:
  Program();
  ~Program();
  void run();

private:
  auto static write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t;
  void static extract_links_rec(lxb_dom_node_t* node, std::vector<PageNode>& out, std::string const&);
  auto request_html(std::string const& url) -> ActualUrl_Content_Pair;
  auto crawl_page(std::string const& url, int max_depth) -> PageNode;
  void print_header();
  auto request_input() -> std::string;
  int request_depth();
  auto parse_url(std::string url, std::string const& content) -> std::vector<PageNode>;
};
