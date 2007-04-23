/*
    This file is part of Orange.

    Orange is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Orange is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Orange; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Authors: Martin Mozina, Janez Demsar, Blaz Zupan, 1996--2004
    Contact: martin.mozina@fri.uni-lj.si
*/

#include "filter.hpp"
#include "table.hpp"
#include "stat.hpp"
#include "measures.hpp"
#include "discretize.hpp"
#include "distvars.hpp"
#include "classfromvar.hpp"
#include "progress.hpp"


#include "rulelearner.ppp"


DEFINE_TOrangeVector_classDescription(PRule, "TRuleList", true, ORANGE_API)
DEFINE_TOrangeVector_classDescription(PEVCDist, "TEVCDistList", true, ORANGE_API)


TRule::TRule()
: weightID(0),
  quality(ILLEGAL_FLOAT),
  complexity(ILLEGAL_FLOAT),
  coveredExamples(NULL),
  coveredExamplesLength(-1),
  parentRule(NULL)
{}


TRule::TRule(PFilter af, PClassifier cl, PLearner lr, PDistribution dist, PExampleTable ce, const int &w, const float &qu)
: filter(af),
  classifier(cl),
  learner(lr),
  classDistribution(dist),
  examples(ce),
  weightID(w),
  quality(qu),
  coveredExamples(NULL),
  coveredExamplesLength(-1),
  parentRule(NULL)
{}


TRule::TRule(const TRule &other, bool copyData)
: filter(other.filter? other.filter->deepCopy() : PFilter()),
  classifier(other.classifier),
  learner(other.learner),
  complexity(other.complexity),
  classDistribution(copyData ? other.classDistribution: PDistribution()),
  examples(copyData ? other.examples : PExampleTable()),
  weightID(copyData ? other.weightID : 0),
  quality(copyData ? other.quality : ILLEGAL_FLOAT),
  coveredExamples(copyData && other.coveredExamples && (other.coveredExamplesLength >= 0) ? (int *)memcpy(new int[other.coveredExamplesLength], other.coveredExamples, other.coveredExamplesLength) : NULL),
  coveredExamplesLength(copyData ? other.coveredExamplesLength : -1),
  parentRule(other.parentRule)
{}


TRule::~TRule()
{ delete coveredExamples; }

bool TRule::operator ()(const TExample &ex)
{
  checkProperty(filter);
  return filter->call(ex);
}


#define HIGHBIT 0x80000000

PExampleTable TRule::operator ()(PExampleTable gen, const bool ref, const bool negate)
{
  checkProperty(filter);

  TExampleTable *table = ref ? mlnew TExampleTable(gen, 1) : mlnew TExampleTable(PExampleGenerator(gen));
  PExampleGenerator wtable = table;

  PEITERATE(ei, gen)
    if (filter->call(*ei) != negate)
      table->addExample(*ei);

  return wtable;
}


void TRule::filterAndStore(PExampleTable gen, const int &wei, const int &targetClass, const int *prevCovered, const int anExamples)
{
  checkProperty(filter);
  examples=this->call(gen);
  weightID = wei;
  classDistribution = getClassDistribution(examples, wei);
  if (classDistribution->abs==0)
    return;

  if (learner) {
    classifier = learner->call(examples,wei);
  }
  else if (targetClass>=0)
    classifier = mlnew TDefaultClassifier(gen->domain->classVar, TValue(targetClass), classDistribution);
  else
    classifier = mlnew TDefaultClassifier(gen->domain->classVar, classDistribution); 
/*  if (anExamples > 0) {
    const int bitsInInt = sizeof(int)*8;
    coveredExamplesLength = anExamples/bitsInInt + 1;
    coveredExamples = (int *)malloc(coveredExamplesLength);
    if (prevCovered) {
      memcpy(coveredExamples, prevCovered, coveredExamplesLength);

      int *cei = coveredExamples-1;
      int mask = 0;
      int inBit = 0;

      PEITERATE(ei, gen) {
        if (!(*cei & mask)) {
          if (inBit)
            *cei = *cei << inBit;
          while(!*++cei);
          mask = -1;
          inBit = bitsInInt;
        }

        while( (*cei & HIGHBIT) == 0) {
          *cei = *cei << 1;
          *cei = mask << 1;
          inBit--;
        }

        if (filter->call(*ei)) {
          *cei = (*cei << 1) | 1;
          table->addExample(*ei);
        }
        else
          *cei = *cei << 1;

        mask = mask << 1;
        inBit--;
      }
    }

    else {
      int *cei = coveredExamples;
      int inBit = bitsInInt;

      PEITERATE(ei, gen) {
        if (filter->call(*ei)) {
          *cei = (*cei << 1) | 1;
          table->addExample(*ei);
        }
        else
          *cei = *cei << 1;

        if (!--inBit) {
          inBit = bitsInInt;
          cei++;
        }
      }
      *cei = *cei << inBit;
    }
  } */
}



bool haveEqualValues(const TRule &r1, const TRule &r2)
{
  const TDefaultClassifier *clsf1 = r1.classifier.AS(TDefaultClassifier);
  const TDefaultClassifier *clsf2 = r2.classifier.AS(TDefaultClassifier);
  if (!clsf1 || !clsf2)
    return false;

  const TDiscDistribution *dist1 = dynamic_cast<const TDiscDistribution *>(clsf1->defaultDistribution.getUnwrappedPtr());
  const TDiscDistribution *dist2 = dynamic_cast<const TDiscDistribution *>(clsf2->defaultDistribution.getUnwrappedPtr());

  float high1 = dist1->highestProb();
  float high2 = dist2->highestProb();

  for(TDiscDistribution::const_iterator d1i(dist1->begin()), d1e(dist1->end()), d2i(dist2->begin()), d2e(dist2->end());
      (d1i!=d1e) && (d2i!=d2e);
      d1i++, d2i++)
    if ((*d1i == high1) && (*d2i == high2))
      return true;

  return false;
}


bool TRule::operator <(const TRule &other) const
{
  if (!haveEqualValues(*this, other))
    return false;

  bool different = false;
  
  if (coveredExamples && other.coveredExamples) {
    int *c1i = coveredExamples;
    int *c2i = other.coveredExamples;
    for(int i = coveredExamplesLength; i--; c1i++, c2i++) {
      if (*c1i & ~*c2i)
        return false;
      if (*c1i != *c2i)
        different = true;
    }
  }
  else {
    raiseError("operator not implemented yet");
  }

  return different;
}   


bool TRule::operator <=(const TRule &other) const
{
  if (!haveEqualValues(*this, other))
    return false;

  if (coveredExamples && other.coveredExamples) {
    int *c1i = coveredExamples;
    int *c2i = other.coveredExamples;
    for(int i = coveredExamplesLength; i--; c1i++, c2i++) {
      if (*c1i & ~*c2i)
        return false;
    }
  }

  else {
    raiseError("operator not implemented yet");
  }

  return true;
}


bool TRule::operator >(const TRule &other) const
{
  if (!haveEqualValues(*this, other))
    return false;

  bool different = false;
  if (coveredExamples && other.coveredExamples) {
    int *c1i = coveredExamples;
    int *c2i = other.coveredExamples;
    for(int i = coveredExamplesLength; i--; c1i++, c2i++) {
      if (~*c1i & *c2i)
        return false;
      if (*c1i != *c2i)
        different = true;
    }
  }

  else {
    raiseError("operator not implemented yet");
  }

  return different;
}   


bool TRule::operator >=(const TRule &other) const
{
  if (!haveEqualValues(*this, other))
    return false;

  if (coveredExamples && other.coveredExamples) {
    int *c1i = coveredExamples;
    int *c2i = other.coveredExamples;
    for(int i = coveredExamplesLength; i--; c1i++, c2i++) {
      if (~*c1i & *c2i)
        return false;
    }
  }

  else {
    raiseError("operator not implemented yet");
  }

  return true;
}


bool TRule::operator ==(const TRule &other) const
{
  if (!haveEqualValues(*this, other))
    return false;

  if (coveredExamples && other.coveredExamples) {
    return !memcmp(coveredExamples, other.coveredExamples, coveredExamplesLength);
  }

  else {
    raiseError("operator not implemented yet");
  }

  return false;
}



TRuleValidator_LRS::TRuleValidator_LRS(const float &a, const float &min_coverage, const float &max_rule_complexity, const float &min_quality)
: alpha(a),
  min_coverage(min_coverage),
  max_rule_complexity(max_rule_complexity),
  min_quality(min_quality)
{}

bool TRuleValidator_LRS::operator()(PRule rule, PExampleTable, const int &, const int &targetClass, PDistribution apriori) const
{
  const TDiscDistribution &obs_dist = dynamic_cast<const TDiscDistribution &>(rule->classDistribution.getReference());
  if (!obs_dist.cases)
    return false;
  
  if (obs_dist.cases < min_coverage)
    return false;

  if (max_rule_complexity > -1.0 && rule->complexity > max_rule_complexity)
    return false;

  if (min_quality>rule->quality)
    return false;

  const TDiscDistribution &exp_dist = dynamic_cast<const TDiscDistribution &>(apriori.getReference());

  if (obs_dist.abs == exp_dist.abs) //it turns out that this happens quite often
    return false; 

  if (alpha >= 1.0)
    return true;

  if (targetClass == -1) {
    float lrs = 0.0;
    for(TDiscDistribution::const_iterator odi(obs_dist.begin()), ode(obs_dist.end()), edi(exp_dist.begin()), ede(exp_dist.end());
        (odi!=ode); odi++, edi++) {
      if ((edi!=ede) && (*edi) && (*odi))
        lrs += *odi * log(*odi / ((edi != ede) & (*edi > 0.0) ? *edi : 1e-5));
    }

    lrs = 2 * (lrs - obs_dist.abs * log(obs_dist.abs / exp_dist.abs));

    return (lrs > 0.0) && (chisqprob(lrs, float(obs_dist.size()-1)) <= alpha);
  }

  float p = (targetClass < obs_dist.size()) ? obs_dist[targetClass] : 1e-5;
  const float P = (targetClass < exp_dist.size()) && (exp_dist[targetClass] > 0.0) ? exp_dist[targetClass] : 1e-5;

  float n = obs_dist.abs - p;
  float N = exp_dist.abs - P;

  if (n>=N)
    return false;

  if (N<=0.0)
    N = 1e-6f;
  if (p<=0.0)
    p = 1e-6f;
  if (n<=0.0)
    n = 1e-6f;
  
  float lrs = 2 * (p*log(p/P) + n*log(n/N) - obs_dist.abs * log(obs_dist.abs/exp_dist.abs));

  return (lrs > 0.0) && (chisqprob(lrs, 1.0f) <= alpha);
}


float TRuleEvaluator_Entropy::operator()(PRule rule, PExampleTable, const int &, const int &targetClass, PDistribution apriori)
{
  const TDiscDistribution &obs_dist = dynamic_cast<const TDiscDistribution &>(rule->classDistribution.getReference());
  if (!obs_dist.cases)
    return -numeric_limits<float>::max();

  if (targetClass == -1)
    return -getEntropy(dynamic_cast<TDiscDistribution &>(rule->classDistribution.getReference()));

  const TDiscDistribution &exp_dist = dynamic_cast<const TDiscDistribution &>(apriori.getReference());

  float p = (targetClass < obs_dist.size()) ? obs_dist[targetClass] : 0.0;
  const float P = (targetClass < exp_dist.size()) && (exp_dist[targetClass] > 0.0) ? exp_dist[targetClass] : 1e-5;

  float n = obs_dist.abs - p;
  float N = exp_dist.abs - P;
  if (N<=0.0)
    N = 1e-6f;
  if (p<=0.0)
    p = 1e-6f;
  if (n<=0.0)
    n = 1e-6f;

  return ((p*log(p) + n*log(n) - obs_dist.abs * log(obs_dist.abs)) / obs_dist.abs);
}

float TRuleEvaluator_Laplace::operator()(PRule rule, PExampleTable, const int &, const int &targetClass, PDistribution apriori)
{
  const TDiscDistribution &obs_dist = dynamic_cast<const TDiscDistribution &>(rule->classDistribution.getReference());
  if (!obs_dist.cases)
    return 0;

  float p;
  if (targetClass == -1) {
    p = float(obs_dist.highestProb());
    return (p+1)/(obs_dist.abs+obs_dist.size());
  }
  p = float(obs_dist[targetClass]);
  return (p+1)/(obs_dist.abs+2);
}

TRuleEvaluator_LRS::TRuleEvaluator_LRS(const bool &sr)
: storeRules(sr)
{
  TRuleList *ruleList = mlnew TRuleList;
  rules = ruleList;
}

float TRuleEvaluator_LRS::operator()(PRule rule, PExampleTable, const int &, const int &targetClass, PDistribution apriori)
{
  const TDiscDistribution &obs_dist = dynamic_cast<const TDiscDistribution &>(rule->classDistribution.getReference());
  if (!obs_dist.cases)
    return 0.0;
  
  const TDiscDistribution &exp_dist = dynamic_cast<const TDiscDistribution &>(apriori.getReference());

  if (obs_dist.abs >= exp_dist.abs) //it turns out that this happens quite often
    return 0.0; 

  if (targetClass == -1) {
    float lrs = 0.0;
    for(TDiscDistribution::const_iterator odi(obs_dist.begin()), ode(obs_dist.end()), edi(exp_dist.begin()), ede(exp_dist.end());
        (odi!=ode); odi++, edi++) {
      if ((edi!=ede) && (*edi) && (*odi))
        lrs += *odi * log(*odi / ((edi != ede) & (*edi > 0.0) ? *edi : 1e-5));
    }
    lrs = 2 * (lrs - obs_dist.abs * log(obs_dist.abs / exp_dist.abs));
    return lrs;
  }

  float p = (targetClass < obs_dist.size()) ? obs_dist[targetClass]-0.5 : 1e-5;
  const float P = (targetClass < exp_dist.size()) && (exp_dist[targetClass] > 0.0) ? exp_dist[targetClass] : 1e-5;

  if (p/obs_dist.abs < P/exp_dist.abs)
    return 0.0;

  float n = obs_dist.abs - p;
  float N = exp_dist.abs - P;

  if (N<=0.0)
    N = 1e-6f;
  if (p<=0.0)
    p = 1e-6f;
  if (n<=0.0)
    n = 1e-6f;

  float lrs = 2 * (p*log(p/P) + n*log(n/N) - obs_dist.abs * log(obs_dist.abs/exp_dist.abs));
  if (storeRules) {
    TRuleList &rlist = rules.getReference();
    rlist.push_back(rule);
  }
  return lrs;
}


TEVCDist::TEVCDist(const float & mu, const float & beta, PFloatList & percentiles) 
: mu(mu),
  beta(beta),
  percentiles(percentiles)
{}

TEVCDist::TEVCDist() 
{}

double TEVCDist::getProb(const float & chi)
{
  if (!percentiles || percentiles->size()==0 || percentiles->at(percentiles->size()-1)<chi)
    return 1.0-exp(-exp((double)(mu-chi)/beta));
  if (chi < percentiles->at(0))
    return 1.0;
  TFloatList::const_iterator pi(percentiles->begin()), pe(percentiles->end());
  for (; (pi+1)!=pe; pi++) {
    float a = *pi;
    float b = *(pi+1);
    if (chi>=a && chi <=b)
      return (chi-a)/(b-a);
  }
  return 1.0;
}

float TEVCDist::median()
{
  if (!percentiles || percentiles->size()==0)
    return mu + beta*0.36651292; // log(log(2))
  return (percentiles->at(4)+percentiles->at(5))/2;
}

TEVCDistGetter_Standard::TEVCDistGetter_Standard(PEVCDistList dists) 
: dists(dists)
{}

TEVCDistGetter_Standard::TEVCDistGetter_Standard()
{}

PEVCDist TEVCDistGetter_Standard::operator()(const PRule, const int & length) const
{
  if (dists->size() > length)
    return dists->at(length);
  return NULL;
}
   
float getChi(float p, float n, float P, float N)
{
  float pn = p+n;
  if (p/(p+n) == P/(P+N))
    return 0.0;
  else if (p/(p+n) < P/(P+N)) {
    p = p+0.5;
    if (p>(p+n)*P/(P+N))
      p = (p+n)*P/(P+N);
    n = pn-p;
  }
  else {
    p = p - 0.5;
    if (p<(p+n)*P/(P+N))
      p = (p+n)*P/(P+N);
    n = pn-p;
  }
  return 2*(p*log(p/(p+n))+n*log(n/(p+n))+(P-p)*log((P-p)/(P+N-p-n))+(N-n)*log((N-n)/(P+N-p-n))-P*log(P/(P+N))-N*log(N/(P+N)));
}

// 2 log likelihood with Yates' correction
float TChiFunction_2LOGLR::operator()(PRule rule, PExampleTable data, const int & weightID, const int & targetClass, PDistribution apriori, float & nonOptimistic_Chi) const
{
  nonOptimistic_Chi = 0.0;
  if (!rule->classDistribution->abs || apriori->abs == rule->classDistribution->abs)
    return 0.0;
  return getChi(rule->classDistribution->atint(targetClass),
                rule->classDistribution->abs - rule->classDistribution->atint(targetClass),
                apriori->atint(targetClass),
                apriori->abs - apriori->atint(targetClass));
}



TRuleEvaluator_mEVC::TRuleEvaluator_mEVC(const int & m, PChiFunction chiFunction, PEVCDistGetter evcDistGetter, PVariable probVar, PRuleValidator validator, const int & min_improved, const float & min_improved_perc)
: m(m),
  chiFunction(chiFunction),
  evcDistGetter(evcDistGetter),
  probVar(probVar),
  validator(validator),
  min_improved(min_improved),
  min_improved_perc(min_improved_perc),
  bestRule(NULL)
{}

TRuleEvaluator_mEVC::TRuleEvaluator_mEVC()
: m(0),
  chiFunction(NULL),
  evcDistGetter(NULL),
  probVar(NULL),
  validator(NULL),
  min_improved(1),
  min_improved_perc(0),
  bestRule(NULL)
{}

void TRuleEvaluator_mEVC::reset()
{
  bestRule = NULL;
}

LNLNChiSq::LNLNChiSq(PEVCDist evc, const float & chi)
: evc(evc),
  chi(chi)
{
  extremeAlpha = evc->getProb(chi);
  if (extremeAlpha < 0.05)
    extremeAlpha = 0.0;
}

double LNLNChiSq::operator()(float chix) const {
    if (chix<=0.0)
        return 100.0;
    double chip = chisqprob((double)chix,1.0); // in statc
    if (extremeAlpha > 0.0)
        return chip-extremeAlpha;
    if (chip<=0.0 && (evc->mu-chi)/evc->beta < -100)
        return 0.0;
    if (chip<=0.0)
        return -100.0;
    if (chip < 1e-6)
        return log(chip)-(evc->mu-chi)/evc->beta;
    return log(-log(1-chip))-(evc->mu-chi)/evc->beta;
}

LRInv::LRInv(const float & pn, const float & P, const float & PN, const float & chiCorrected)
: pn(pn),
  P(P), 
  chiCorrected(chiCorrected)
{
  N = PN - P;
}

double LRInv::operator()(float p) const {
    return getChi(p,pn-p,P,N) - chiCorrected;
}

// Implementation of Brent's root finding method.
float brent(const float & minv, const float & maxv, const int & maxsteps, DiffFunc * func) 
{
    float a = minv;
    float b = maxv;
    float fa = func->call(a);
    float fb = func->call(b);
    if (fb>0 && fa>0 && fb>fa || fb<0 && fa<0 && fb<fa)
        return a;
    if (fb>0 && fa>0 && fb<fa || fb<0 && fa<0 && fb>fa)
        return b;

    float c = a; // c is previous value of b
    float fe, fc = fa;
    float m = 0.0, e = 0.0, d = 0.0;
    int counter = 0;
    while (1) {
        counter += 1;
        if (fb == fa)
          return b;
        else if (fb!=fc && fa!=fc)
            d = a*fb*fc/(fa-fb)/(fa-fc)+b*fa*fc/(fb-fa)/(fb-fc)+c*fa*fb/(fc-fa)/(fc-fb);
        else
            d = b-fb*(b-a)/(fb-fa);
        m = (a+b)/2;
        if (d<=m && d>=b || d>=m && d<=b)
            e = d;
        else
            e = m;
        fe = func->call(e);
        if (fe*fb<0) {
            a = b;
            fa = fb;
        }
        c = b;
        fc = fb;
        b = e;
        fb = fe;
        if (abs(a-b)<0.01 && fa*fb<0)
            return (a+b)/2.;
        if (fb*fa>0 || b>maxv || b<minv)
            return 0.0;
        if ((b>0.1 && fb*func->call(b-0.1)<=0) || fb*func->call(b+0.1)<=0)
            return b;
        if (counter>maxsteps)
            return 0.0;
    }
}

float TRuleEvaluator_mEVC::evaluateRule(PRule rule, PExampleTable examples, const int & weightID, const int &targetClass, PDistribution apriori, const int & rLength, const float & aprioriProb) const
{
  PEVCDist evc = evcDistGetter->call(rule, rLength);
  if (!evc || evc->mu < 0.0)
    return -10e+6;
  if (evc->mu == 0.0 || rLength == 0)
    return (rule->classDistribution->atint(targetClass)+m*aprioriProb)/(rule->classDistribution->abs+m);
  PEVCDist evc_inter = evcDistGetter->call(rule, 0);
  float rule_acc = rule->classDistribution->atint(targetClass)/rule->classDistribution->abs;
  // if accuracy of rule is worse than prior probability
  if (rule_acc < aprioriProb)
    return rule_acc - 0.01;
  // correct chi square
  float nonOptimistic_Chi = 0.0;

  float chi = chiFunction->call(rule, examples, weightID, targetClass, apriori, nonOptimistic_Chi);
  if ((evc->mu-chi)/evc->beta < -100)
    return (rule->classDistribution->atint(targetClass)+m*aprioriProb)/(rule->classDistribution->abs+m);

  float median = evc->median();
  float chiCorrected = nonOptimistic_Chi;
  // chi is less then median ..
  if (chi <= median)
    return aprioriProb-0.01;

  // correct chi
  LNLNChiSq *diffFunc = new LNLNChiSq(evc,chi);
  chiCorrected += brent(0.0,chi,100, diffFunc);
  delete diffFunc;

  // remove inter-length optimism
  chiCorrected -= evc_inter->mu;
  rule->chi = chiCorrected;
  // compute expected number of positive examples
  float ePositives = 0.0;
  if (chiCorrected > 0.0)
  {
    LRInv *diffFunc = new LRInv(rule->classDistribution->abs,apriori->atint(targetClass),apriori->abs,chiCorrected);
    ePositives = brent(0.0, rule->classDistribution->atint(targetClass), 100, diffFunc);
    delete diffFunc;
  }
  float quality = (ePositives + m*aprioriProb)/(rule->classDistribution->abs+m);
  if (quality > aprioriProb)
    return quality;
  return aprioriProb-0.01;
}

float TRuleEvaluator_mEVC::operator()(PRule rule, PExampleTable examples, const int & weightID, const int &targetClass, PDistribution apriori)
{
  rule->chi = 0.0;
  if (!rule->classDistribution->cases || !rule->classDistribution->atint(targetClass))
    return 0;

  // evaluate rule
  TFilter_values *filter = rule->filter.AS(TFilter_values);
  int rLength = filter->conditions->size();
  float aprioriProb = apriori->atint(targetClass)/apriori->abs;
  rule->quality = evaluateRule(rule,examples,weightID,targetClass,apriori,rLength,aprioriProb);
  if (rule->quality < 0.0)
    return rule->quality;
  if (!probVar)
    return rule->quality;

  // get rule's probability coverage
  float requiredQuality = 0.0;
  int improved = 0;
  PEITERATE(ei, rule->examples) {
    if ((*ei).getClass().intV != targetClass)
      continue;
    if (rule->quality > (*ei)[probVar].floatV)
      improved ++;
    requiredQuality += (*ei)[probVar].floatV; 
  }
  requiredQuality /= rule->classDistribution->atint(targetClass);

  // compute future quality
  float futureQuality = 0.0;
  if (requiredQuality <= rule->quality)
    futureQuality = 1+rule->quality;
  else {
    PDistribution oldRuleDist = rule->classDistribution;
    rule->classDistribution = mlnew TDiscDistribution(examples->domain->classVar);
    rule->classDistribution->setint(targetClass, oldRuleDist->atint(targetClass));
    rule->classDistribution->abs = rule->classDistribution->atint(targetClass);
    float bestQuality = evaluateRule(rule,examples,weightID,targetClass,apriori,rLength+1,aprioriProb);
    rule->classDistribution = oldRuleDist;
    if (bestQuality < rule->quality)
      futureQuality = -1;
    else if (bestQuality < requiredQuality || (bestRule && bestQuality <= bestRule->quality))
      futureQuality = -1;
    else
      futureQuality = (bestQuality-requiredQuality)/(bestQuality-rule->quality);
  }

  // store best rule and return result
  if (improved >= min_improved && improved/rule->classDistribution->atint(targetClass) > min_improved_perc &&
      rule->quality > aprioriProb &&
      (!bestRule || (rule->quality>bestRule->quality)) &&
      (!validator || validator->call(rule, examples, weightID, targetClass, apriori))) {
      TRule *pbestRule = new TRule(rule.getReference(), true);
      bestRule = pbestRule;
  }
  return futureQuality;
}

bool worstRule(const PRule &r1, const PRule &r2)
{ return    (r1->quality > r2->quality) 
          || (r1->quality==r2->quality 
          && r1->complexity < r2->complexity);
}
/*         || (r1->quality==r2->quality) 
            && (   (r1->complexity < r2->complexity)
                || (r1->complexity == r2->complexity) 
                   && ((int(r1.getUnwrappedPtr()) ^ int(r2.getUnwrappedPtr())) & 16) != 0
               ); }  */

bool inRules(PRuleList rules, PRule rule) 
{
  TRuleList::const_iterator ri(rules->begin()), re(rules->end());
  PExampleGenerator rulegen = rule->examples;
  for (; ri!=re; ri++) {
    PExampleGenerator rigen = (*ri)->examples;
    if (rigen->numberOfExamples() == rulegen->numberOfExamples()) {
      TExampleIterator rei(rulegen->begin()), ree(rulegen->end());
      TExampleIterator riei(rigen->begin()), riee(rigen->end());
      for (; rei != ree && !(*rei).compare(*riei); ++rei, ++riei) {
      }
        if (rei == ree)
          return true;
    }
  }
  return false;
}

TRuleBeamFilter_Width::TRuleBeamFilter_Width(const int &w)
: width(w)
{}


void TRuleBeamFilter_Width::operator()(PRuleList &rules, PExampleTable, const int &)
{
  if (rules->size() > width) {
    // Janez poglej
    sort(rules->begin(), rules->end(), worstRule);
  
    TRuleList *filteredRules = mlnew TRuleList;
    PRuleList wFilteredRules = filteredRules;

    int nRules = 0;
    TRuleList::const_iterator ri(rules->begin()), re(rules->end());
    while (nRules < width && ri != re) {
      if (!inRules(wFilteredRules,*ri)) {
        wFilteredRules->push_back(*ri);
        nRules++;
      }
      ri++;
    }
    rules =  wFilteredRules;  
  }
}


inline void _selectBestRule(PRule &rule, PRule &bestRule, int &wins, TRandomGenerator &rgen)
{
  if ((rule->quality > bestRule->quality) || (rule->complexity < bestRule->complexity)) {
    bestRule = rule;
    wins = 1;
  }
  else if ((rule->complexity == bestRule->complexity) && rgen.randbool(++wins))
    bestRule = rule;
}



PRuleList TRuleBeamInitializer_Default::operator()(PExampleTable data, const int &weightID, const int &targetClass, PRuleList baseRules, PRuleEvaluator evaluator, PDistribution apriori, PRule &bestRule)
{
  checkProperty(evaluator);

  TRuleList *ruleList = mlnew TRuleList();
  PRuleList wruleList = ruleList;

  TRandomGenerator rgen(data->numberOfExamples());
  int wins;

  if (baseRules && baseRules->size())
    PITERATE(TRuleList, ri, baseRules) {
      TRule *newRule = mlnew TRule((*ri).getReference(), true);
      PRule wNewRule = newRule;
      ruleList->push_back(wNewRule);
      if (!newRule->examples)
        newRule->filterAndStore(data,weightID,targetClass);
      newRule->quality = evaluator->call(wNewRule, data, weightID, targetClass, apriori);
      if (!bestRule || (newRule->quality > bestRule->quality)) {
        bestRule = wNewRule;
        wins = 1;
      }
      else 
        if (newRule->quality == bestRule->quality)
          _selectBestRule(wNewRule, bestRule, wins, rgen);
    }

  else {
     TRule *ubestRule = mlnew TRule();
     bestRule = ubestRule;
     ruleList->push_back(bestRule);
     ubestRule->filter = new TFilter_values();
     ubestRule->filter->domain = data->domain;
     ubestRule->filterAndStore(data, weightID,targetClass);
     ubestRule->complexity = 0;
  }

  return wruleList;
}


PRuleList TRuleBeamRefiner_Selector::operator()(PRule wrule, PExampleTable data, const int &weightID, const int &targetClass)
{
  if (!discretization) {
    discretization = mlnew TEntropyDiscretization();
    dynamic_cast<TEntropyDiscretization *>(discretization.getUnwrappedPtr())->forceAttribute = true;
  }

  TRule &rule = wrule.getReference();
  TFilter_values *filter = wrule->filter.AS(TFilter_values);
  if (!filter)
    raiseError("a filter of type 'Filter_values' expected");

  TRuleList *ruleList = mlnew TRuleList;
  PRuleList wRuleList = ruleList;

  TDomainDistributions ddist(wrule->examples, wrule->weightID);

  const TVarList &attributes = rule.examples->domain->attributes.getReference();

  vector<bool> used(attributes.size(), false);
  PITERATE(TValueFilterList, vfi, filter->conditions)
    used[(*vfi)->position] = true;

  vector<bool>::const_iterator ui(used.begin());
  TDomainDistributions::const_iterator di(ddist.begin());
  TVarList::const_iterator vi(attributes.begin()), ve(attributes.end());
  int pos = 0;
  for(; vi != ve; vi++, ui++, pos++, di++) {
    if ((*vi)->varType == TValue::INTVAR) {
      if (!*ui) {
        vector<float>::const_iterator idi((*di).AS(TDiscDistribution)->begin());
        for(int v = 0, e = (*vi)->noOfValues(); v != e; v++)
          if (*idi>0) {
            TRule *newRule = mlnew TRule(rule, false);
            ruleList->push_back(newRule);
            newRule->complexity++;

            filter = newRule->filter.AS(TFilter_values);

            TValueFilter_discrete *newCondition = mlnew TValueFilter_discrete(pos, *vi, 0);
            filter->conditions->push_back(newCondition);

            TValue value = TValue(v);
            newCondition->values->push_back(value);
            newRule->filterAndStore(rule.examples, rule.weightID,targetClass);
            newRule->parentRule = wrule;
          }
      }
    }

    else if (((*vi)->varType == TValue::FLOATVAR)) {
      if (discretization) {
        PVariable discretized;
        try {
          discretized = discretization->call(rule.examples, *vi, weightID);
        } catch(...) {
          continue;
        }
        TClassifierFromVar *cfv = discretized->getValueFrom.AS(TClassifierFromVar);
        TDiscretizer *discretizer = cfv ? cfv->transformer.AS(TDiscretizer) : NULL;
        if (!discretizer)
          raiseError("invalid or unrecognized discretizer");

        vector<float> cutoffs;
        discretizer->getCutoffs(cutoffs);
        if (cutoffs.size()) {
          TRule *newRule;
          newRule = mlnew TRule(rule, false);
          PRule wnewRule = newRule;
          newRule->complexity++;
          newRule->parentRule = wrule;

          newRule->filter.AS(TFilter_values)->conditions->push_back(mlnew TValueFilter_continuous(pos,  TValueFilter_continuous::LessEqual, cutoffs.front(), 0, 0));
          newRule->filterAndStore(rule.examples, rule.weightID,targetClass);
          if (wrule->classDistribution->cases > wnewRule->classDistribution->cases)
            ruleList->push_back(newRule);

          for(vector<float>::const_iterator ci(cutoffs.begin()), ce(cutoffs.end()-1); ci != ce; ci++) {
            newRule = mlnew TRule(rule, false);
            wnewRule = newRule;
            newRule->complexity++;
            newRule->parentRule = wrule;
            filter = newRule->filter.AS(TFilter_values);
            filter->conditions->push_back(mlnew TValueFilter_continuous(pos,  TValueFilter_continuous::Greater, *ci, 0, 0));
            newRule->filterAndStore(rule.examples, rule.weightID,targetClass);
            if (wrule->classDistribution->cases > wnewRule->classDistribution->cases)
              ruleList->push_back(newRule);

            newRule = mlnew TRule(rule, false);
            wnewRule = newRule;
            newRule->complexity++;
            newRule->parentRule = wrule;
            filter = newRule->filter.AS(TFilter_values);
            filter->conditions->push_back(mlnew TValueFilter_continuous(pos,  TValueFilter_continuous::LessEqual, *(ci+1), 0, 0));
            newRule->filterAndStore(rule.examples, rule.weightID,targetClass);
            if (wrule->classDistribution->cases > wnewRule->classDistribution->cases)
              ruleList->push_back(newRule);
          } 

          newRule = mlnew TRule(rule, false);
          ruleList->push_back(newRule);
          newRule->complexity++;

          newRule->filter.AS(TFilter_values)->conditions->push_back(mlnew TValueFilter_continuous(pos,  TValueFilter_continuous::Greater, cutoffs.back(), 0, 0));
          newRule->filterAndStore(rule.examples, rule.weightID,targetClass);
          newRule->parentRule = wrule;
        } 
      }
      else
        raiseWarning("discretizer not given, continuous attributes will be skipped"); 
    } 
  }
  if (!discretization)
    discretization = PDiscretization();
  return wRuleList;
}


PRuleList TRuleBeamCandidateSelector_TakeAll::operator()(PRuleList &existingRules, PExampleTable, const int &)
{
  PRuleList candidates = mlnew TRuleList(existingRules.getReference());
//  existingRules->clear();
  existingRules->erase(existingRules->begin(), existingRules->end());
  return candidates;
}


PRule TRuleBeamFinder::operator()(PExampleTable data, const int &weightID, const int &targetClass, PRuleList baseRules)
{
  // set default values if value not set
  bool tempInitializer = !initializer;
  if (tempInitializer)
    initializer = mlnew TRuleBeamInitializer_Default;
  bool tempCandidateSelector = !candidateSelector;
  if (tempCandidateSelector)
    candidateSelector = mlnew TRuleBeamCandidateSelector_TakeAll;
  bool tempRefiner = !refiner;
  if (tempRefiner)
    refiner = mlnew TRuleBeamRefiner_Selector;
/*  bool tempValidator = !validator;
  if (tempValidator) 
    validator = mlnew TRuleValidator_LRS((float)0.01);
  bool tempRuleStoppingValidator = !ruleStoppingValidator;
  if (tempRuleStoppingValidator) 
    ruleStoppingValidator = mlnew TRuleValidator_LRS((float)0.05); */
  bool tempEvaluator = !evaluator;
  if (tempEvaluator)
    evaluator = mlnew TRuleEvaluator_Entropy;
  bool tempRuleFilter = !ruleFilter;
  if (tempRuleFilter)
    ruleFilter = mlnew TRuleBeamFilter_Width;

  checkProperty(initializer);
  checkProperty(candidateSelector);
  checkProperty(refiner);
  checkProperty(evaluator);
  checkProperty(ruleFilter);

  PDistribution apriori = getClassDistribution(data, weightID);

  TRandomGenerator rgen(data->numberOfExamples());
  int wins = 1;

  PRule bestRule;
  PRuleList ruleList = initializer->call(data, weightID, targetClass, baseRules, evaluator, apriori, bestRule);

  {
  PITERATE(TRuleList, ri, ruleList) {
    if (!(*ri)->examples)
      (*ri)->filterAndStore(data, weightID,targetClass);
    if ((*ri)->quality == ILLEGAL_FLOAT)
      (*ri)->quality = evaluator->call(*ri, data, weightID, targetClass, apriori);
  }
  }

  if (!bestRule->examples)
    bestRule->filterAndStore(data, weightID,targetClass);
  if (bestRule->quality == ILLEGAL_FLOAT)
    bestRule->quality = evaluator->call(bestRule, data, weightID, targetClass, apriori);

  int bestRuleLength = 0;
  while(ruleList->size()) {
    PRuleList candidateRules = candidateSelector->call(ruleList, data, weightID);
    PITERATE(TRuleList, ri, candidateRules) {
      PRuleList newRules = refiner->call(*ri, data, weightID, targetClass);      
      PITERATE(TRuleList, ni, newRules) {
        (*ni)->quality = evaluator->call(*ni, data, weightID, targetClass, apriori);
        if ((*ni)->quality >= bestRule->quality && (!validator || validator->call(*ni, data, weightID, targetClass, apriori)))
          _selectBestRule(*ni, bestRule, wins, rgen);
        if (!ruleStoppingValidator || ruleStoppingValidator->call(*ni, (*ri)->examples, weightID, targetClass, (*ri)->classDistribution)) {
          ruleList->push_back(*ni);
        }
      }  
    } 
    ruleFilter->call(ruleList,data,weightID);
  }

  // set empty values if value was not set (used default)
  if (tempInitializer)
    initializer = PRuleBeamInitializer();
  if (tempCandidateSelector)
    candidateSelector = PRuleBeamCandidateSelector();
  if (tempRefiner)
    refiner = PRuleBeamRefiner();
/*  if (tempValidator)
    validator = PRuleValidator();
  if (tempRuleStoppingValidator)
    ruleStoppingValidator = PRuleValidator();  */
  if (tempEvaluator)
    evaluator = PRuleEvaluator();
  if (tempRuleFilter)
    ruleFilter = PRuleBeamFilter();

  return bestRule;
}


TRuleLearner::TRuleLearner(bool se, int tc, PRuleList rl)
: storeExamples(se),
  targetClass(tc),
  baseRules(rl)
{}


PClassifier TRuleLearner::operator()(PExampleGenerator gen, const int &weightID)
{
  return this->call(gen,weightID,targetClass,baseRules);
}

PClassifier TRuleLearner::operator()(PExampleGenerator gen, const int &weightID, const int &targetClass, PRuleList baseRules)
{
  // Initialize default values if values not set
  bool tempDataStopping = !dataStopping && !ruleStopping;
  if (tempDataStopping) 
    dataStopping = mlnew TRuleDataStoppingCriteria_NoPositives;

  bool tempRuleFinder = !ruleFinder;
  if (tempRuleFinder)
    ruleFinder = mlnew TRuleBeamFinder;

  bool tempCoverAndRemove = !coverAndRemove;
  if (tempCoverAndRemove)
    coverAndRemove = mlnew TRuleCovererAndRemover_Default;

  checkProperty(ruleFinder);
  checkProperty(coverAndRemove);

  TExampleTable *data = mlnew TExampleTable(gen);
  PExampleTable wdata = data;

  if (!dataStopping && !ruleStopping)
    raiseError("no stopping criteria; set 'dataStopping' and/or 'ruleStopping'");

  TRuleList *ruleList = mlnew TRuleList;
  PRuleList wruleList = ruleList;

  int currWeightID = weightID;

  float beginwe=0.0, currentwe;
  if (progressCallback) {
    if (targetClass==-1)
      beginwe = wdata->weightOfExamples(weightID);
    else {
      PDistribution classDist = getClassDistribution(wdata, weightID);
      TDiscDistribution *ddist = classDist.AS(TDiscDistribution);
      beginwe = ddist->atint(targetClass);
    }
    progressCallback->call(0.0);
  }

  while (!dataStopping || !dataStopping->call(wdata, currWeightID, targetClass)) {
    PRule rule = ruleFinder->call(wdata, currWeightID, targetClass, baseRules);
    if (!rule)
      raiseError("'ruleFinder' didn't return a rule");

    if (ruleStopping && ruleStopping->call(ruleList, rule, wdata, currWeightID))
      break;

    wdata = coverAndRemove->call(rule, wdata, currWeightID, currWeightID, targetClass);
    ruleList->push_back(rule);

    if (progressCallback) {
      if (targetClass==-1)
        currentwe = wdata->weightOfExamples(weightID);
      else {
        PDistribution classDist = getClassDistribution(wdata, currWeightID);
        TDiscDistribution *ddist = classDist.AS(TDiscDistribution);
        currentwe = ddist->atint(targetClass);
      }
      progressCallback->call(1-currentwe/beginwe);
    }
  }
  if (progressCallback)
    progressCallback->call(1.0);


  // Restore values
  if (tempDataStopping) 
    dataStopping = PRuleDataStoppingCriteria();
  if (tempRuleFinder)
    ruleFinder = PRuleFinder();
  if (tempCoverAndRemove)
    coverAndRemove = PRuleCovererAndRemover();

  PRuleClassifierConstructor clConstructor = 
    classifierConstructor ? classifierConstructor : 
    PRuleClassifierConstructor(mlnew TRuleClassifierConstructor_firstRule());
  return clConstructor->call(ruleList, gen, weightID);
};


bool TRuleDataStoppingCriteria_NoPositives::operator()(PExampleTable data, const int &weightID, const int &targetClass) const
{
  PDistribution classDist = getClassDistribution(data, weightID);
  TDiscDistribution *ddist = classDist.AS(TDiscDistribution);

  return (targetClass >= 0 ? ddist->atint(targetClass) : ddist->abs) == 0.0;
}

bool TRuleStoppingCriteria_NegativeDistribution::operator()(PRuleList ruleList, PRule rule, PExampleTable data, const int &weightID) const
{
  if (rule && rule->classifier) 
  {
    PDistribution aprioriDist = getClassDistribution(data, weightID);
    TDiscDistribution *apriori = aprioriDist.AS(TDiscDistribution);

    const TDefaultClassifier *clsf = rule->classifier.AS(TDefaultClassifier);
    if (!clsf)
      return false;
    const TDiscDistribution *dist = dynamic_cast<const TDiscDistribution *>(clsf->defaultDistribution.getUnwrappedPtr());
    const int classVal = clsf->defaultVal.intV;
    if (classVal<0 || classVal>=dist->size())
      return false;
    float acc = dist->atint(clsf->defaultVal.intV)/dist->abs;
    float accApriori = apriori->atint(clsf->defaultVal.intV)/apriori->abs;
    if (accApriori>acc)
      return true;
  }
  return false;
}


PExampleTable TRuleCovererAndRemover_Default::operator()(PRule rule, PExampleTable data, const int &weightID, int &newWeight, const int &targetClass) const
{
  TExampleTable *table = mlnew TExampleTable(data, 1);
  PExampleGenerator wtable = table;

  TFilter &filter = rule->filter.getReference();

  if (targetClass < 0)
  {
    PEITERATE(ei, data)
      if (!filter(*ei))
        table->addExample(*ei);
  }
  else 
    PEITERATE(ei, data)
      if (!filter(*ei) || (*ei).getClass().intV!=targetClass)
        table->addExample(*ei);


  newWeight = weightID;
  return wtable;  
}

// classifiers
PRuleClassifier TRuleClassifierConstructor_firstRule::operator ()(PRuleList rules, PExampleTable table, const int &weightID)
{
  return mlnew TRuleClassifier_firstRule(rules, table, weightID);
}


TRuleClassifier::TRuleClassifier(PRuleList arules, PExampleTable anexamples, const int &aweightID)
: rules(arules),
  examples(anexamples),
  weightID(aweightID),
  TClassifier(anexamples->domain->classVar,true)
{}

TRuleClassifier::TRuleClassifier()
: TClassifier(true)
{}


TRuleClassifier_firstRule::TRuleClassifier_firstRule(PRuleList arules, PExampleTable anexamples, const int &aweightID)
: TRuleClassifier(arules, anexamples, aweightID)
{
  prior = getClassDistribution(examples, weightID);
}

TRuleClassifier_firstRule::TRuleClassifier_firstRule()
: TRuleClassifier()
{}

PDistribution TRuleClassifier_firstRule::classDistribution(const TExample &ex)
{
  checkProperty(rules);
  checkProperty(prior);

  PITERATE(TRuleList, ri, rules) {
    if ((*ri)->call(ex))
      return (*ri)->classDistribution;
  }
  return prior;
}

void copyTable(float **dest, float **source, int nx, int ny) {
  for (int i=0; i<nx; i++)
    memcpy(dest[i],source[i],ny*sizeof(float));
}

// Rule classifier based on logit (beta) coefficients
TRuleClassifier_logit::TRuleClassifier_logit(PRuleList arules, const float &minBeta, PExampleTable anexamples, const int &aweightID, const PClassifier &classifier, const PDistributionList &probList, const bool &anuseBestRuleOnly)
: TRuleClassifier(arules, anexamples, aweightID),
  minBeta(minBeta),
  useBestRuleOnly(anuseBestRuleOnly),
  priorClassifier(classifier)
{
  // compute prior distribution of learning examples
  prior = getClassDistribution(examples, weightID);
  domain = examples->domain;

  // initialize variables f, p, tempF, tempP, wavgCov, wavgCovPrior, wsd, wsdPrior,
  // ruleIndices, betas, priorBetas, wpriorProb, wavgProb
  initialize(probList);
  float step = 2.0;

  // compute initial goodness-of-fit evaluation
  eval = compPotEval(0,getClassIndex(*(rules->begin())),betas[0],tempF,tempP,wavgProb,wpriorProb);
  //raiseWarning("rule 0 prob: %f, rule 0 beta: %f",wavgProb->at(0),betas[0]);

  // set up prior Betas
  while (step > 0.004) {
    step /= 2.0;
    correctPriorBetas(step);
  }
  eval = compPotEval(0,getClassIndex(*(rules->begin())),betas[0],tempF,tempP,wavgProb,wpriorProb);
  //raiseWarning("rule 0 prob: %f, rule 0 beta: %f",wavgProb->at(0),betas[0]);

  // evaluation loop
  float **oldF, **oldP, *oldBetas, *oldPriorBetas; 
  oldF = new float*[examples->domain->classVar->noOfValues()-1];
  oldP = new float*[examples->domain->classVar->noOfValues()];
  oldBetas = new float[rules->size()];
  oldPriorBetas = new float[examples->domain->classVar->noOfValues()];
  int i;
  for(i=0; i<examples->domain->classVar->noOfValues(); i++) {
    if (i<examples->domain->classVar->noOfValues()-1)
      oldF[i] = new float[examples->numberOfExamples()];
    oldP[i] = new float[examples->numberOfExamples()];
  }
//  raiseWarning("rule 0 prob: %f, rule 0 beta: %f",wavgProb->at(0),betas[0]);

  float priorb = priorBetas[0];
  step = 2.0;
  while (step > 0.004) {
    step /= 2.0;
    bool improvedOverAll = true;
    bool beenInCorrectPrior = false;
    float oldEval = eval;
//    raiseWarning("rule 0 prob: %f, rule 0 beta: %f, step: %f",wavgProb->at(0),betas[0],step);
  	while (improvedOverAll) {
	  	updateRuleBetas(step);
//      raiseWarning("rule 0 prob: %f, rule 0 beta: %f out",wavgProb->at(0),betas[0],step);
		  // optimize prior betas
		  if (eval<=oldEval && beenInCorrectPrior) {
		    eval = oldEval;
        copyTable(f, oldF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
        copyTable(p, oldP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
        copyTable(tempF, oldF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
        copyTable(tempP, oldP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
        memcpy(betas,oldBetas,sizeof(float)*rules->size());
		    memcpy(priorBetas,oldPriorBetas,sizeof(float)*(examples->domain->classVar->noOfValues()-1));
		    improvedOverAll = false;
		  }
		  else {
        beenInCorrectPrior = true;
		    // store old Values
        copyTable(oldF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
        copyTable(oldP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
		    oldEval = eval;
		    memcpy(oldBetas,betas,sizeof(float)*rules->size());
		    memcpy(oldPriorBetas,priorBetas,sizeof(float)*(examples->domain->classVar->noOfValues()-1));
		    // first correct prior prob deviations
		    correctPriorBetas(step);
		    if (oldEval != eval) // prior probs changed
			    continue;
		    // if no change in prior betas - try to distort them
		    distortPriorBetas(step);
//        raiseWarning("rule 0 prob: %f, rule 0 beta: %f in",wavgProb->at(0),betas[0],step);
      }
    }
  }

  TFloatList *aruleBetas = mlnew TFloatList();
  ruleBetas = aruleBetas;
  TFloatList *apriorProbBetas = mlnew TFloatList();
  priorProbBetas = apriorProbBetas;
  for (i=0; i<rules->size(); i++)
    ruleBetas->push_back(betas[i]>minBeta?betas[i]:0);
  for (i=0; i<examples->domain->classVar->noOfValues()-1; i++)
    priorProbBetas->push_back(priorBetas[i]);

  delete [] oldBetas;
  delete [] oldPriorBetas;
  for(i=0; i<examples->domain->classVar->noOfValues(); i++) {
    if (i<examples->domain->classVar->noOfValues()-1)
      delete [] oldF[i];
    delete [] oldP[i];
  }
  delete [] oldF; delete [] oldP;
}

TRuleClassifier_logit::TRuleClassifier_logit()
: TRuleClassifier()
{}


//==============================================================================
// return 1 if system not solving
// nDim - system dimension
// pfMatr - matrix with coefficients
// pfVect - vector with free members
// pfSolution - vector with system solution
// pfMatr becames trianglular after function call
// pfVect changes after function call
//
// Developer: Henry Guennadi Levkin
//
//==============================================================================
int LinearEquationsSolving(int nDim, double* pfMatr, double* pfVect, double* pfSolution)
{
  double fMaxElem;
  double fAcc;

  int i, j, k, m;


  for(k=0; k<(nDim-1); k++) // base row of matrix
  {
    // search of line with max element
    fMaxElem = fabs( pfMatr[k*nDim + k] );
    m = k;
    for(i=k+1; i<nDim; i++)
    {
      if(fMaxElem < fabs(pfMatr[i*nDim + k]) )
      {
        fMaxElem = pfMatr[i*nDim + k];
        m = i;
      }
    }
    
    // permutation of base line (index k) and max element line(index m)
    if(m != k)
    {
      for(i=k; i<nDim; i++)
      {
        fAcc               = pfMatr[k*nDim + i];
        pfMatr[k*nDim + i] = pfMatr[m*nDim + i];
        pfMatr[m*nDim + i] = fAcc;
      }
      fAcc = pfVect[k];
      pfVect[k] = pfVect[m];
      pfVect[m] = fAcc;
    }

    if( pfMatr[k*nDim + k] == 0.) return 1; // needs improvement !!!

    // triangulation of matrix with coefficients
    for(j=(k+1); j<nDim; j++) // current row of matrix
    {
      fAcc = - pfMatr[j*nDim + k] / pfMatr[k*nDim + k];
      for(i=k; i<nDim; i++)
      {
        pfMatr[j*nDim + i] = pfMatr[j*nDim + i] + fAcc*pfMatr[k*nDim + i];
      }
      pfVect[j] = pfVect[j] + fAcc*pfVect[k]; // free member recalculation
    }
  }

  for(k=(nDim-1); k>=0; k--)
  {
    pfSolution[k] = pfVect[k];
    for(i=(k+1); i<nDim; i++)
    {
      pfSolution[k] -= (pfMatr[k*nDim + i]*pfSolution[i]);
    }
    pfSolution[k] = pfSolution[k] / pfMatr[k*nDim + k];
  }

  return 0;
}

// function sums; f = a0 + a1*r1.quality + ... AND example probabilities 
// set all to zero
// Compute average example coverage and set index of examples covered by rule
// set all remaining variables
void TRuleClassifier_logit::initialize(const PDistributionList &probList)
{
  psize = examples->domain->classVar->noOfValues()*examples->numberOfExamples();
  fsize = (examples->domain->classVar->noOfValues()-1)*examples->numberOfExamples();

  // initialize f, p, tempF, tempP
  f = new float *[examples->domain->classVar->noOfValues()-1];
  p = new float *[examples->domain->classVar->noOfValues()];
  tempF = new float *[examples->domain->classVar->noOfValues()-1];
  tempP = new float *[examples->domain->classVar->noOfValues()];
  int i, j;
  for (i=0; i<examples->domain->classVar->noOfValues()-1; i++) {
	  f[i] = new float[examples->numberOfExamples()];
	  p[i] = new float[examples->numberOfExamples()];
	  tempF[i] = new float[examples->numberOfExamples()];
	  tempP[i] = new float[examples->numberOfExamples()];
	  for (int j=0; j<examples->numberOfExamples(); j++) {
  		  f[i][j] = 0.0;
	  	  p[i][j] = 1.0/examples->domain->classVar->noOfValues();
		    tempF[i][j] = 0.0;
		    tempP[i][j] = 1.0/examples->domain->classVar->noOfValues();
	  }
  }
  {
	  p[examples->domain->classVar->noOfValues()-1] = new float[examples->numberOfExamples()];
	  tempP[examples->domain->classVar->noOfValues()-1] = new float[examples->numberOfExamples()];
	  for (int j=0; j<examples->numberOfExamples(); j++) {
		  p[examples->domain->classVar->noOfValues()-1][j] = 1.0/examples->domain->classVar->noOfValues();
		  tempP[examples->domain->classVar->noOfValues()-1][j] = 1.0/examples->domain->classVar->noOfValues();
	  }
  }

   // if initial example probability is given, update F and P
  if (probList) {
    double *matrix = new double [pow(examples->domain->classVar->noOfValues()-1,2)];
    double *fVals = new double [examples->domain->classVar->noOfValues()-1];
    double *results = new double [examples->domain->classVar->noOfValues()-1];
    for (i=0; i<probList->size(); i++) {
      int k1, k2;
      TDistribution *dist = mlnew TDiscDistribution(probList->at(i)->variable);
      PDistribution wdist = dist;

      for (k1=0; k1<examples->domain->classVar->noOfValues(); k1++) {
        if (probList->at(i)->atint(k1) >= 1.0-1e-4)
          wdist->setint(k1,(float)(1.0-1e-4));
        else if (probList->at(i)->atint(k1) <= 1e-4)
          wdist->setint(k1,(float)(1e-4));
        else
          wdist->setint(k1,probList->at(i)->atint(k1));
      }
      wdist->normalize();
      for (k1=0; k1<examples->domain->classVar->noOfValues()-1; k1++) {
        fVals[k1] = -wdist->atint(k1);
        for (k2=0; k2<examples->domain->classVar->noOfValues()-1; k2++) {
          if (k1==k2)
            matrix[k1*(examples->domain->classVar->noOfValues()-1)+k2] = wdist->atint(k1)-1;
          else
            matrix[k1*(examples->domain->classVar->noOfValues()-1)+k2] = wdist->atint(k1);
        }
      }
      LinearEquationsSolving(examples->domain->classVar->noOfValues()-1, matrix, fVals, results);
      for (k1=0; k1<examples->domain->classVar->noOfValues()-1; k1++) {
        f[k1][i] = results[k1]>0.0 ? log(results[k1]) : -10.0;
  		  tempF[k1][i] = f[k1][i];
      }
      for (k1=0; k1<examples->domain->classVar->noOfValues(); k1++) {
  		  p[k1][i] = wdist->atint(k1);
  		  tempP[k1][i] = wdist->atint(k1);
      }
    }
    delete [] matrix;
    delete [] fVals;
    delete [] results;
  }

  // Compute average example coverage and set index of examples covered by rule
  float *coverages = new float[examples->numberOfExamples()];
  for (j=0; j<examples->numberOfExamples(); j++) {
    coverages[j] = 1.0;
  }
  i=0;
  ruleIndices = mlnew PIntList[rules->size()];
  {
    PITERATE(TRuleList, ri, rules) {
      TIntList *ruleIndicesnw = mlnew TIntList();
      ruleIndices[i] = ruleIndicesnw;
      j=0;
      PEITERATE(ei, examples) {
        if ((*ri)->call(*ei)) {
	        ruleIndices[i]->push_back(j);
          //int vv = (*ei).getClass().intV;
		      if ((*ei).getClass().intV == getClassIndex(*ri))
			      coverages[j] += 1.0;
        }
	      j++;
      }
      i++;
    }
  }

  TFloatList *avgCov = new TFloatList();
  wavgCov = avgCov;
  TFloatList *avgCovPrior = new TFloatList();
  wavgCovPrior = avgCovPrior;
  for (i=0; i<rules->size(); i++) {
    float newCov = 0.0;
    float counter = 0.0;
    PITERATE(TIntList, ind, ruleIndices[i]) 
      if (getClassIndex(rules->at(i)) == examples->at(*ind).getClass().intV) {
        newCov += coverages[*ind];
        counter++;
      }
    if (counter) {
      wavgCov->push_back(newCov/counter);
    }
    else
      wavgCov->push_back(0.0);
  }
  for (i=0; i<examples->domain->classVar->noOfValues(); i++) {
    float newCov = 0.0;
    float counter = 0.0;
    for (j=0; j<examples->numberOfExamples(); j++)
      if (examples->at(j).getClass().intV == i) {
        newCov += coverages[j];
        counter++;
      }
    if (counter)
      wavgCovPrior->push_back(newCov/counter);
    else
      wavgCovPrior->push_back(0.0);
  }

  // compute standard deviations
  TFloatList *sd = new TFloatList(); 
  wsd = sd;
  PITERATE(TRuleList, ri, rules) {
    float n = (*ri)->examples->numberOfExamples();
    float a = n*(*ri)->quality;
    float b = n*(1.0-(*ri)->quality);
    float expab = log(a)+log(b)-2*log(a+b)-log(a+b+1);
    wsd->push_back(exp(0.5*expab));
  }
  TFloatList *sdPrior = new TFloatList(); 
  wsdPrior = sdPrior;
  for (i=0; i<examples->domain->classVar->noOfValues(); i++) {
    float n = examples->numberOfExamples();
    float a = n*prior->atint(i)/prior->abs;
    float b = n*(1.0-prior->atint(i)/prior->abs);
    float expab = log(a)+log(b)-2*log(a+b)-log(a+b+1);
    wsdPrior->push_back(exp(0.5*expab));
  }

  // set initial values of betas (as minbetas)
/*  minbetas = new float[rules->size()];
  for (i=0; i<rules->size(); i++) {
    PRule r = rules->at(i);
    float p0 = prior->atint(getClassIndex(r))/prior->abs;
    p0 = p0>1e-6 ? p0 : 1e-6; p0 = p0<1.0-1e-6 ? p0 : 1.0-1e-6;
    float p = r->quality;
    p = p>1e-6 ? p : 1e-6; p = p<1.0-1e-6 ? p : 1.0-1e-6;
    minbetas[i] = log((1-p0)/(1-p)*p/p0)/(wavgCov->at(i));
  } */
  betas = new float[rules->size()];
  for (i=0; i<rules->size(); i++)
	  betas[i] = minBeta;//0.0;//minbetas[i];

  // Add default rules
  priorBetas = new float[examples->domain->classVar->noOfValues()];
  for (i=0; i<examples->domain->classVar->noOfValues(); i++)
	  priorBetas[i] = 0.0;

  // priorProb and avgProb
  TFloatList *priorProb = mlnew TFloatList();
  wpriorProb = priorProb;
  TFloatList *avgProb = mlnew TFloatList();
  wavgProb = avgProb;

  // set coveredRules to examples
  coveredRules = new PIntList *[examples->numberOfExamples()];
  for (i=0; i<examples->numberOfExamples(); i++) {
    coveredRules[i] = new PIntList [examples->domain->classVar->noOfValues()];
    for (int j=0; j<examples->domain->classVar->noOfValues(); j++) {
      TIntList *tmpList = new TIntList();
      coveredRules[i][j] = tmpList;
      for (int k=0; k<rules->size(); k++) {
        if (rules->at(k)->call(examples->at(i)) && getClassIndex(rules->at(k))==j)
          coveredRules[i][j]->push_back(k);
      }
    }
  }
}

float TRuleClassifier_logit::cutOptimisticBetas(float step, float curr_eval)
{
 float newEval = curr_eval;
 bool changedOptimistic = true;
  while (changedOptimistic) {
    changedOptimistic = false;
  	for (int i=0; i<rules->size(); i++)
      if ((wavgProb->at(i) > rules->at(i)->quality) && (betas[i]-step)>=minBeta) {
        newEval = compPotEval(i, getClassIndex(rules->at(i)), betas[i]-step,tempF,tempP,wavgProb,wpriorProb);
				betas[i] = betas[i]-step;
        changedOptimistic = true;
      }
  }
 //raiseWarning("rule 0 prob: %f, rule 0 beta: %f",wavgProb->at(0),betas[0]);
  return newEval;
}

// Iterates through rules and tries to change betas to improve goodness-of-fit
void TRuleClassifier_logit::updateRuleBetas(float step)
{
  // cut betas of optimistic rules (also copy from tempF to f - cutOptimisticBetas does not)
/*  if (step>=1.0)
    raiseWarning("before cut rule 0 prob: %f, rule 0 beta: %f, step: %f, counter: ",wavgProb->at(0),betas[0],step);*/
  eval = cutOptimisticBetas(step, eval);
  copyTable(f, tempF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
  copyTable(p, tempP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
  /*if (step>=1.0)
    raiseWarning("after cut rule 0 prob: %f, rule 0 beta: %f, step: %f, counter: ",wavgProb->at(0),betas[0],step);*/

  float *oldBetasU = new float[rules->size()];
 	bool changed = true;
  int counter = 0;
  while (changed && counter < 10) { // 10 steps should be perfectly enough as steps are halved - teoretically we only need 1 step
/*    if (counter >= 1.0)
      raiseWarning("rule 0 prob: %f, rule 0 beta: %f, step: %f, counter: %f",wavgProb->at(0),betas[0],step, counter); */
    counter += 1;
		changed = false;
    for (int i=0; i<rules->size(); i++) {
			// positive update of beta
			bool improve = false;
/*      if (step>=1.0 && i<2 && counter < 2)
        raiseWarning("after cut 1 rule 0 prob: %f, rule 0 beta: %f, step: %f, counter: %d, %d",wavgProb->at(0),betas[0],step, counter,i);*/
			float newEval = compPotEval(i, getClassIndex(rules->at(i)), betas[i]+step,tempF,tempP,wavgProb,wpriorProb);
/*      if (step>=1.0 && i<2 && counter < 2)
        raiseWarning("after cut 2 rule 0 prob: %f, rule 0 beta: %f, step: %f, counter: %d, %d",wavgProb->at(0),betas[0],step, counter,i);*/
      if (newEval>eval && wavgProb->at(i) <= rules->at(i)->quality) { //  
		    memcpy(oldBetasU,betas,sizeof(float)*rules->size());
				betas[i] = betas[i]+step;
        newEval = cutOptimisticBetas(step, newEval);
        if (newEval>eval) {
				  eval = newEval;
				  // tempP and tempF are set in compPotEval
          copyTable(f, tempF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
          copyTable(p, tempP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
				  improve = true;
				  changed = true;
        }
        else {
          memcpy(betas,oldBetasU,sizeof(float)*rules->size());
          copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
          copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
        }
			}
      else {
        copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
        copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
      }
		}
	}
  delete [] oldBetasU;
}

// Correct prior probabilities by setting prior betas.
void TRuleClassifier_logit::correctPriorBetas(float step)
{
	bool changed = true;
  int counter = 0;
  while (changed && counter<10) { // for counter see updateRulesBeta method
    counter ++;
		changed = false;
		for (int i=0; i<examples->domain->classVar->noOfValues()-1; i++) {
		// positive update of prior
			bool improve = false;
			priorBetas[i]+=step;
			float newEval = compPotEval(0, getClassIndex(rules->at(0)), betas[0],tempF,tempP,wavgProb,wpriorProb);
			if (wpriorProb->at(i) <= prior->atint(i)/prior->abs) {
				newEval = cutOptimisticBetas(step, newEval);
        if (newEval > eval) {
          eval = newEval;
          copyTable(p, tempP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
          copyTable(f, tempF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
				  improve = true;
				  changed = true;
        }
        else {
          priorBetas[i]-=step;
          copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
          copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
        }
			}
      else {
				priorBetas[i]-=step;
        copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
        copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
      }
			if (!improve) {
				priorBetas[i]-=step;
				newEval = compPotEval(0, getClassIndex(rules->at(0)), betas[0],tempF,tempP,wavgProb,wpriorProb);
				if (wpriorProb->at(i) >= prior->atint(i)/prior->abs) {
					newEval = cutOptimisticBetas(step, newEval);
          if (newEval > eval) {
            eval = newEval;
            copyTable(p, tempP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
            copyTable(f, tempF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
					  changed = true;
            improve = true;
          }
          else {
            priorBetas[i]+=step;
            copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
            copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
          }
				}
				else 
					priorBetas[i]+=step;
          copyTable(tempP, p, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
          copyTable(tempF, f, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
			}
    }
	}
}

// Distort probabilities a bit and hope to get out of local maximum
void TRuleClassifier_logit::distortPriorBetas(float step)
{
	// compute prior probabilities
	float *priors = new float[examples->domain->classVar->noOfValues()];
  float sum = 1.0;
  int i;
	for (i=0; i<examples->domain->classVar->noOfValues()-1; i++) {
    priors[i] = exp(priorBetas[i]);
    sum += priors[i];
  }
  priors[examples->domain->classVar->noOfValues()-1] = 1.0;
	for (i=0; i<examples->domain->classVar->noOfValues(); i++)
    priors[i]/=sum;

	// get worse class
	int worstClassInd=0;
	float worstClassDiff, currDiff;
	
	for (i=0; i<examples->domain->classVar->noOfValues()-1; i++) {
		if (i==0)
			worstClassDiff=abs(prior->atint(0)/prior->abs-priors[0]);
		currDiff=abs(prior->atint(i)/prior->abs-priors[i]);
		if (currDiff>worstClassDiff) {
			worstClassDiff = currDiff;
			worstClassInd = i;
		}
	}

  if (prior->atint(worstClassInd)/prior->abs-priors[worstClassInd]>0.0)
  	priorBetas[worstClassInd] += step;
  else
  	priorBetas[worstClassInd] -= step;
	eval = compPotEval(0, getClassIndex(rules->at(0)), betas[0], tempF,tempP,wavgProb,wpriorProb);
  eval = cutOptimisticBetas(step, eval);
  copyTable(p, tempP, examples->domain->classVar->noOfValues(), examples->numberOfExamples());
  copyTable(f, tempF, examples->domain->classVar->noOfValues()-1, examples->numberOfExamples());
	delete [] priors;
}

float TRuleClassifier_logit::findMax(int clIndex, int exIndex) {
  float maxB = 0.0;
  PITERATE(TIntList, ri, coveredRules[exIndex][clIndex]) {
    if (betas[*ri] > maxB)
      maxB = betas[*ri];
  }
  return maxB;
}

// Computes new probabilities of examples if rule would have beta set as newBeta.
float TRuleClassifier_logit::compPotEval(int ruleIndex, int classIndex, float newBeta, float **tempF, float **tempP, PFloatList &wavgProb, PFloatList &wpriorProb)
{
  float dif = 0.0;
  if (betas[ruleIndex] > minBeta && newBeta > minBeta)
    dif = newBeta - betas[ruleIndex];
  else if (betas[ruleIndex] > minBeta) {
    dif = -betas[ruleIndex];
  }
  else if (newBeta > minBeta)
    dif = newBeta;
/*  if (ruleIndex == 0)
    raiseWarning("dif = %f, %d, %d", dif, useBestRuleOnly, classIndex);*/
  // prepare new probabilities
  if (abs(dif)>1e-10)
    PITERATE(TIntList, ind, ruleIndices[ruleIndex]) {
      if (useBestRuleOnly) {
        dif = betas[ruleIndex];
        betas[ruleIndex] = newBeta;
        for (int fi=0; fi<examples->domain->classVar->noOfValues()-1; fi++) {
          tempF[fi][*ind] = 0.0;
          for (int ci=0; ci<examples->domain->classVar->noOfValues(); ci++) {
            float maxRuleBeta = findMax(ci,*ind);
	          if (ci == fi)
              tempF[fi][*ind] += maxRuleBeta>minBeta?maxRuleBeta:0.0;
	          else
		          tempF[fi][*ind] -= maxRuleBeta>minBeta?maxRuleBeta:0.0;
          }
        }
        betas[ruleIndex] = dif;
      }
      else
        for (int fi=0; fi<examples->domain->classVar->noOfValues()-1; fi++)
          if (fi == classIndex) {
/*            if (ruleIndex == 0)
              raiseWarning("oldF = %f, newF = %f", tempF[fi][*ind], tempF[fi][*ind] + dif);*/
	          tempF[fi][*ind] += dif;
          }
	        else
		        tempF[fi][*ind] -= dif;
      float sum = 1.0;
      int pi;
      for (pi=0; pi<examples->domain->classVar->noOfValues()-1; pi++) {
        tempP[pi][*ind] = exp(tempF[pi][*ind]+priorBetas[pi]);
        sum += exp(tempF[pi][*ind]+priorBetas[pi]);
      }
      tempP[examples->domain->classVar->noOfValues()-1][*ind] = 1.0;
      for (pi=0; pi<examples->domain->classVar->noOfValues(); pi+=1)
        tempP[pi][*ind] /= sum;
    }
  else {
    for (int ei=0; ei<examples->numberOfExamples(); ei++) {
      float sum = 1.0;
      int pi;
      for (pi=0; pi<examples->domain->classVar->noOfValues()-1; pi++) {
        tempP[pi][ei] = exp(tempF[pi][ei]+priorBetas[pi]);
        sum += exp(tempF[pi][ei]+priorBetas[pi]);
      }
      tempP[examples->domain->classVar->noOfValues()-1][ei] = 1.0;
      for (pi=0; pi<examples->domain->classVar->noOfValues(); pi+=1)
        tempP[pi][ei] /= sum;
    }
  }
  
  // compute new rule avgProbs
  wavgProb->clear();
  int classInd = 0;
  float newAvgProb = 0.0;
  for (int ri = 0; ri<rules->size(); ri++) {
    newAvgProb = 0.0;
    classInd = getClassIndex(rules->at(ri));
    PITERATE(TIntList, ind, ruleIndices[ri]) {
      newAvgProb += tempP[classInd][*ind];
    }
    wavgProb->push_back(newAvgProb/ruleIndices[ri]->size());
  }

  // compute new prior probs
  wpriorProb->clear();
  for (int pi=0; pi<examples->domain->classVar->noOfValues(); pi++) {
    float newPriorProb = 0.0;
    for (int ei=0; ei<examples->numberOfExamples(); ei++) {
      newPriorProb += tempP[pi][ei];

    }
    wpriorProb->push_back(newPriorProb/examples->numberOfExamples());
  }

  // new evaluation
  float newEval = 0.0;
  for (int ei=0; ei<examples->numberOfExamples(); ei++) {
    newEval += tempP[examples->at(ei).getClass().intV][ei]>0.0 ? log(tempP[examples->at(ei).getClass().intV][ei]) : -1e+6;
//    newEval -= pow(1.0-tempP[examples->at(ei).getClass().intV][ei],2);
  }


/*  float newEval = 0.0;
  TFloatList::iterator sdi(wsd->begin()), sde(wsd->end());
  TFloatList::iterator aci(wavgCov->begin()), ace(wavgCov->end());
  TFloatList::iterator api(wavgProb->begin()), ape(wavgProb->end());
  TRuleList::iterator rit(rules->begin()), re(rules->end());
  int bi = 0;
  for (; rit!=re; rit++,sdi++,aci++,api++) {
    if (!((*api)>(*rit)->quality && (ruleIndex==bi && newBeta>minBeta || betas[bi]>minBeta))) {
      int nExamples = (*rit)->examples->numberOfExamples();
      float quality = (*rit)->quality;
      if ((*api)>quality)
        newEval += 0.1*(nExamples*quality*log((*api)/quality)+nExamples*(1-quality)*log((1-(*api))/(1-quality)))/(*aci);
      else
        newEval += (nExamples*quality*log((*api)/quality)+nExamples*(1-quality)*log((1-(*api))/(1-quality)))/(*aci);
    }
    bi++;
  } */

  // new evaluation from prior
/*  sdi = wsdPrior->begin(); sde = wsdPrior->end();
  aci = wavgCovPrior->begin(); ace = wavgCovPrior->end();
  api = wpriorProb->begin(); ape = wpriorProb->end();
  for (int i=0; api!=ape; i++, api++,sdi++,aci++) {
    int nExamples = examples->numberOfExamples();
    float quality = prior->atint(i)/prior->abs;
    newEval += (nExamples*quality*log((*api)/quality)+nExamples*(1-quality)*log((1-(*api))/(1-quality)))/(*aci);
  } */
  return newEval;
}

void TRuleClassifier_logit::addPriorClassifier(const TExample &ex, double * priorFs) {
  // initialize variables
  double *matrix = new double [pow(examples->domain->classVar->noOfValues()-1,2)];
  double *fVals = new double [examples->domain->classVar->noOfValues()-1];
  double *results = new double [examples->domain->classVar->noOfValues()-1];
  int k1, k2;
  TDistribution *dist = mlnew TDiscDistribution(domain->classVar);
  PDistribution wdist = dist;

  PDistribution classifierDist = priorClassifier->classDistribution(ex);
  // correct probablity if equals 1.0
  for (k1=0; k1<examples->domain->classVar->noOfValues(); k1++) {
    if (classifierDist->atint(k1) >= 1.0-1e-4)
      wdist->setint(k1,(float)(1.0-1e-4));
    else if (classifierDist->atint(k1) <= 1e-4)
      wdist->setint(k1,(float)(1e-4));
    else
      wdist->setint(k1,classifierDist->atint(k1));
  }
  wdist->normalize();

  // create matrix
  for (k1=0; k1<examples->domain->classVar->noOfValues()-1; k1++) {
    fVals[k1] = -wdist->atint(k1);
    for (k2=0; k2<examples->domain->classVar->noOfValues()-1; k2++) {
      if (k1==k2)
        matrix[k1*(examples->domain->classVar->noOfValues()-1)+k2] = wdist->atint(k1)-1;
      else
        matrix[k1*(examples->domain->classVar->noOfValues()-1)+k2] = wdist->atint(k1);
    }
  }
  // solve equation
  LinearEquationsSolving(examples->domain->classVar->noOfValues()-1, matrix, fVals, results);
  for (k1=0; k1<examples->domain->classVar->noOfValues()-1; k1++)
    priorFs[k1] = results[k1]>0.0 ? log(results[k1]) : -10.0;
  // clean up
  delete [] matrix;
  delete [] fVals;
  delete [] results;
}

PDistribution TRuleClassifier_logit::classDistribution(const TExample &ex)
{
  checkProperty(rules);
  checkProperty(prior);
  checkProperty(domain);
  TExample cexample(domain, ex);

  TDiscDistribution *dist = mlnew TDiscDistribution(domain->classVar);
  PDistribution res = dist;

  // if correcting a classifier, use that one first then
  double * priorFs = new double [examples->domain->classVar->noOfValues()-1];
  if (priorClassifier)
    addPriorClassifier(ex, priorFs);
  else
    for (int k=0; k<examples->domain->classVar->noOfValues()-1; k++)
      priorFs[k] = 0.0;

  // find best beta influence (logit)
  float *bestBeta = new float [domain->classVar->noOfValues()];
  if (useBestRuleOnly) {
    for (int i=0; i<domain->classVar->noOfValues(); i++)
      bestBeta[i]=0.0;
    TFloatList::const_iterator b(ruleBetas->begin()), be(ruleBetas->end());
    TRuleList::iterator r(rules->begin()), re(rules->end());
	  for (; r!=re; r++, b++)
      if ((*r)->call(cexample) && (*b)>bestBeta[getClassIndex(*r)])
        bestBeta[getClassIndex(*r)] = *b;
  }
  // compute return probabilities
  for (int i=0; i<res->noOfElements()-1; i++) {
    float f = priorProbBetas->at(i) + priorFs[i];
    TFloatList::const_iterator b(ruleBetas->begin()), be(ruleBetas->end());
    TRuleList::iterator r(rules->begin()), re(rules->end());
    if (useBestRuleOnly)
      for (int j=0; j<domain->classVar->noOfValues(); j++)
        if (j == i)
    		    f += bestBeta[j]; 
          else
            f -= bestBeta[j];
    else
	    for (; r!=re; r++, b++)
        if ((*r)->call(cexample))
          if (getClassIndex(*r) == i)
    		    f += (*b); 
          else
            f -= (*b); 
    dist->addint(i,exp(f));
  }
  dist->addint(res->noOfElements()-1,1.0);
  dist->normalize();
  delete [] bestBeta;
  delete [] priorFs;
  return res;
}

int TRuleClassifier_logit::getClassIndex(PRule r) {
  const TDefaultClassifier &cl = dynamic_cast<const TDefaultClassifier &>(r->classifier.getReference());
  return cl.defaultVal.intV;
}

// Clear allocated vectors
TRuleClassifier_logit::~TRuleClassifier_logit()
{
  int i;
  for (i=0; i<examples->domain->classVar->noOfValues()-1; i++) {
  	delete [] f[i];
    delete [] tempF[i];
  }
  delete [] f;
  delete [] tempF;

  for (i=0; i<examples->domain->classVar->noOfValues(); i++) {
  	delete [] p[i];
    delete [] tempP[i];
  }
  delete [] p;
  delete [] tempP;

  delete [] betas;
  delete [] priorBetas;
//  delete [] minbetas;
  delete [] ruleIndices;
  for (i=0; i<examples->numberOfExamples(); i++) {
  	delete [] coveredRules[i];
  }
  delete [] coveredRules;
}

TRuleClassifier_logit_bestRule::TRuleClassifier_logit_bestRule()
: TRuleClassifier_logit()
{}

TRuleClassifier_logit_bestRule::TRuleClassifier_logit_bestRule(PRuleList arules, const float &minBeta, PExampleTable anexamples, const int &aweightID, const PClassifier &classifier,const PDistributionList &probList)
: TRuleClassifier_logit(arules, minBeta, anexamples, aweightID, classifier, probList, true)
{}


