#pragma once

#include <string>
#include <vector>
//
#include <lexbor/dom/interfaces/node.h>
#include <curl/curl.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/parser.h>

class PageNode
{
public:
  PageNode(std::string url) :
    m_url{url}, m_links{}
  {
  }

  ~PageNode()
  {
  }

  void add(PageNode node) { m_links.push_back(node); }
  void reserve(std::size_t size) { m_links.reserve(size); };
  auto children() -> std::vector<PageNode> const& { return m_links; }
  auto url() -> std::string const& { return m_url; }

private:
  std::string m_url;
  std::vector<PageNode> m_links;
};

class Program
{
  using FinalURL = std::string;
  using PageContent = std::string;

public:
  using Response = std::pair<FinalURL, PageContent>;

  Program();
  ~Program();
  void run();

  // flow
  void static print_header();
  auto static request_input() -> std::string;
  auto static request_depth() -> int;
  auto request_html(std::string const& url) -> Response;
  auto parse_url(std::string url, std::string const& content) -> std::vector<PageNode>;

  // helpers
  auto static write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t; // used by request_html
  void static extract_links_rec(lxb_dom_node_t* node, std::vector<PageNode>& out, std::string const&);
  auto static crawl_page(std::string const& url, int max_depth) -> PageNode;
  bool static is_valid_url(std::string url);

private:
  CURLM* multi_handle = nullptr;
};
