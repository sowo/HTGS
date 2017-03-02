//
// Created by tjb3 on 3/1/17.
//

#ifndef HTGS_RULEEDGE_HPP
#define HTGS_RULEEDGE_HPP

#include "EdgeDescriptor.hpp"
#include <htgs/api/ITask.hpp>
#include <htgs/core/graph/AnyTaskGraph.hpp>
#include <htgs/api/Bookkeeper.hpp>

namespace htgs {

template <class T, class U, class W>
class RuleEdge : public EdgeDescriptor
{
 public:

  RuleEdge(Bookkeeper<T> *bookkeeper, std::shared_ptr<IRule<T, U>> rule, ITask<U, W> *consumer) : bookkeeper(bookkeeper), rule(rule), consumer(consumer) {}

  ~RuleEdge() override {}

  void applyEdge(AnyTaskGraph *graph) override {
    graph->getTaskScheduler(bookkeeper);
    TaskScheduler<U, W> *consumerTaskScheduler = graph->getTaskScheduler(consumer);

    auto connector = consumerTaskScheduler->getInputConnector();

    if (connector == nullptr)
    {
      connector = std::shared_ptr<Connector<U>>(new Connector<U>());
    }

    RuleScheduler<T, U> *ruleScheduler = new RuleScheduler<T, U>(rule);
    ruleScheduler->setOutputConnector(connector);

    connector->incrementInputTaskCount();

    consumerTaskScheduler->setInputConnector(connector);
    bookkeeper->addRuleScheduler(ruleScheduler);
  }

  EdgeDescriptor *copy(AnyTaskGraph *graph) override {
    return new RuleEdge<T, U, W>((Bookkeeper<T> *)graph->getCopy(bookkeeper), rule, graph->getCopy(consumer));
  }

 private:
  Bookkeeper<T> *bookkeeper;
  std::shared_ptr<IRule<T, U>> rule;
  ITask<U, W> *consumer;
};
}
#endif //HTGS_RULEEDGE_HPP
