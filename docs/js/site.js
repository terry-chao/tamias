/* Tamias 官网 / 文档站的小交互（渐进增强：没有 JS 也能正常读文档）。 */
(function () {
  'use strict';

  function ready(fn) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', fn);
    } else {
      fn();
    }
  }

  ready(function () {
    var body = document.body;
    var header = document.getElementById('tm-header');
    var isHome = body.classList.contains('homepage');

    /* 首页顶部：暗色 hero 上让顶栏透明，滚下去再变实心 */
    if (header && isHome) {
      var syncHeader = function () {
        header.classList.toggle('is-over', window.scrollY < 40);
      };
      syncHeader();
      window.addEventListener('scroll', syncHeader, { passive: true });
    }

    /* 顶栏汉堡菜单（小屏） */
    var navToggle = document.querySelector('[data-tm-toggle="nav"]');
    var nav = document.getElementById('tm-nav');
    if (navToggle && nav) {
      navToggle.addEventListener('click', function () {
        var open = nav.classList.toggle('is-open');
        navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      });
    }

    /* 文档目录折叠（小屏） */
    var treeToggle = document.querySelector('[data-tm-toggle="tree"]');
    var tree = document.getElementById('tm-doc-tree');
    if (treeToggle && tree) {
      treeToggle.addEventListener('click', function () {
        var open = tree.classList.toggle('is-open');
        treeToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      });
    }

    /* 宽表格加横向滚动容器，窄屏不撑破版心 */
    var tables = document.querySelectorAll('.tm-doc-body table');
    for (var i = 0; i < tables.length; i++) {
      var table = tables[i];
      if (table.parentNode && table.parentNode.classList.contains('tm-table-wrap')) {
        continue;
      }
      var wrap = document.createElement('div');
      wrap.className = 'tm-table-wrap';
      table.parentNode.insertBefore(wrap, table);
      wrap.appendChild(table);
    }

    /* 上一页 / 下一页快捷键：主题自带的脚本只认 .navbar 里的 rel=prev/next，
       这里换成页面底部的翻页链接。 */
    var shortcuts = window.shortcuts || null;
    if (shortcuts) {
      document.addEventListener('keydown', function (event) {
        var target = event.target;
        if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
          return;
        }
        var key = event.which || event.keyCode;
        var rel = null;
        if (key === shortcuts.next) {
          rel = 'next';
        } else if (key === shortcuts.previous) {
          rel = 'prev';
        }
        if (!rel) {
          return;
        }
        var link = document.querySelector('.tm-doc-body a[rel="' + rel + '"]');
        if (link && link.href) {
          window.location.href = link.href;
        }
      });
    }
  });
})();
