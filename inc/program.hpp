#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <deque>
#include <optional>
//
#include <curl/curl.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/parser.h>

class PageNode
{
public:
  using Index = int;

  PageNode(int index, int depth) :
    m_links{}, m_index{index}, m_depth{depth}
  {
  }

  ~PageNode()
  {
  }

  void add_link(int link) { m_links.push_back(link); }
  void reserve(std::size_t size) { m_links.reserve(size); };
  auto children() const -> std::vector<int> const& { return m_links; }
  auto index() const -> int const { return m_index; }
  auto depth() const -> int const { return m_depth; }

private:
  int m_index{};
  int m_depth{};
  std::vector<int> m_links;
};

class Program
{
  using FinalURL = std::string;
public:
  using PageContent = std::string;
  using URL = std::string;
  using Response = std::pair<FinalURL, PageContent>;

  Program();
  ~Program();
  void run();

  // flow
  void static print_header();
  auto static request_input() -> std::string;
  auto static request_depth() -> int;
  auto request_html(std::string const& url) -> Response;
  auto static parse_url(std::string url, std::string const& content) -> std::unordered_set<FinalURL>;
  void crawl_page(std::string const& url, int depth);

  // helpers
  auto static write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t; // used by request_html
  void static extract_links_rec(lxb_dom_node_t* node, std::unordered_set<FinalURL>& out, std::string const&);
  auto crawl_page_rec(PageNode&, int depth)-> std::optional<PageNode::Index>;
  bool static is_valid_url(std::string url);
  auto static resolve_url(const std::string& base_url, const std::string& href) -> std::optional<std::string>;
  void graph();

  auto get_node(PageNode::Index) -> PageNode&;
  auto add_node(std::string const&, int depth) -> PageNode::Index;
  auto node_count() -> int { return m_nodes.size(); };
  auto exists(std::string const& url) -> bool;
  auto get_url(PageNode::Index index) -> URL { return m_index_to_url.at(index); }
  auto get_effective_url(std::string const&) -> std::optional<std::string>;

private:
  CURLM* m_multi_handle = nullptr;
  std::unordered_map<std::string, int> m_url_to_index;
  std::unordered_map<int, std::string> m_index_to_url;
  std::deque<PageNode> m_nodes;
};
